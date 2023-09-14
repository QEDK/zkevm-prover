#include <unistd.h>
#include "hashdb64_workflow_test.hpp"
#include "zklog.hpp"
#include "zkresult.hpp"
#include "hashdb_factory.hpp"
#include "utils.hpp"
#include "tree_64.hpp"
#include "hashdb_singleton.hpp"
#include "timer.hpp"

uint64_t HashDB64WorkflowTest (const Config& config)
{
    TimerStart(HASHDB64_WORKFLOW_TEST);

    zklog.info("HashDB64WorkflowTest() started");
    Goldilocks fr;
    PoseidonGoldilocks poseidon;
    //uint64_t tx = 0;
    zkresult zkr;
    Persistence persistence = PERSISTENCE_DATABASE;
    HashDBInterface* pHashDB = HashDBClientFactory::createHashDBClient(fr, config);
    zkassertpermanent(pHashDB != NULL);
    uint64_t flushId, storedFlushId;
    

    const uint64_t numberOfSetsPerTx = 10;
    const uint64_t numberOfTxsPerBatch = 100;

    SmtSetResult setResult;
    SmtGetResult getResult;

    Goldilocks::Element key[4]={0,0,0,0};
    Goldilocks::Element root[4]={0,0,0,0};
    Goldilocks::Element newRoot[4]={0,0,0,0};
    Goldilocks::Element keyfea[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    mpz_class value = 0;
    mpz_class keyScalar = 0;

    // Start batch
    string batchUUID = getUUID();
    Goldilocks::Element batchOldStateRoot[4];
    for (uint64_t i=0; i<4; i++) batchOldStateRoot[i] = root[i];
    vector<KeyValue> keyValues;

    // Set TXs
    for (uint64_t tx=0; tx<numberOfTxsPerBatch; tx++)
    {
        for (uint64_t set=0; set<numberOfSetsPerTx; set++)
        {
            keyScalar++;
            keyfea[0] = fr.fromU64(keyScalar.get_ui());
            poseidon.hash(key, keyfea);
            //scalar2key(fr, keyScalar, key);
            value++;
            
            zkr = pHashDB->set(batchUUID, tx, root, key, value, persistence, newRoot, &setResult, NULL);
            //zklog.info("SET zkr=" + zkresult2string(zkr) + " root=" + fea2string(fr, root) + " key=" + fea2string(fr, key) + " value=" + value.get_str() + " newRoot=" + fea2string(fr, newRoot));
            zkassertpermanent(zkr==ZKR_SUCCESS);
            for (uint64_t i=0; i<4; i++) root[i] = setResult.newRoot[i];
            zkassertpermanent(!fr.isZero(root[0]) || !fr.isZero(root[1]) || !fr.isZero(root[2]) || !fr.isZero(root[3]));

            zkr = pHashDB->get(batchUUID, root, key, value, &getResult, NULL);
            //zklog.info("GET zkr=" + zkresult2string(zkr) + " root=" + fea2string(fr, root) + " key=" + fea2string(fr, key) + " value=" + value.get_str());
            zkassertpermanent(zkr==ZKR_SUCCESS);
            zkassertpermanent(value==getResult.value);

            // Take note of the key we used
            KeyValue keyValue;
            for (uint64_t i=0; i<4; i++) keyValue.key[i] = key[i];
            keyValues.emplace_back(keyValue);
        }

        pHashDB->semiFlush(batchUUID, fea2string(fr, root), persistence);
    }

    // Purge
    zkr = pHashDB->purge(batchUUID, root, persistence);
    zkassertpermanent(zkr==ZKR_SUCCESS);
    zklog.info("PURGE zkr=" + zkresult2string(zkr) + " root=" + fea2string(fr, root) + " flushId=" + to_string(flushId) + " storedFlushId=" + to_string(storedFlushId));

    // Consolidate state root
    Goldilocks::Element consolidatedStateRoot[4];
    zkr = pHashDB->consolidateState(root, persistence, consolidatedStateRoot, flushId, storedFlushId);
    zkassertpermanent(zkr==ZKR_SUCCESS);
    zklog.info("CONSOLIDATE zkr=" + zkresult2string(zkr) + " virtualRoot=" + fea2string(fr, root) + " consolidatedRoot=" + fea2string(fr, consolidatedStateRoot) + " flushId=" + to_string(flushId) + " storedFlushId=" + to_string(storedFlushId));

    // New state root
    Goldilocks::Element batchNewStateRoot[4];
    for (uint64_t i=0; i<4; i++) batchNewStateRoot[i] = consolidatedStateRoot[i];

    // Wait for data to be sent
    while (true)
    {
        uint64_t storedFlushId, storingFlushId, lastFlushId, pendingToFlushNodes, pendingToFlushProgram, storingNodes, storingProgram;
        string proverId;
        zkr = pHashDB->getFlushStatus(storedFlushId, storingFlushId, lastFlushId, pendingToFlushNodes, pendingToFlushProgram, storingNodes, storingProgram, proverId);
        zkassertpermanent(zkr==ZKR_SUCCESS);
        zklog.info("GET FLUSH STATUS storedFlushId=" + to_string(storedFlushId));
        if (storedFlushId >= flushId)
        {
            break;
        }
        sleep(1);
    }
    zklog.info("FLUSHED");

    // Call ReadTree with the old state root to get the hashes of the initial values of all read or written keys
    vector<HashValueGL> oldHashValues;
    zkr = pHashDB->readTree(batchOldStateRoot, keyValues, oldHashValues);
    zkassertpermanent(zkr==ZKR_SUCCESS);
    zklog.info("READ TREE batchOldStateRoot=" + fea2string(fr, batchOldStateRoot) + " keyValues.size=" + to_string(keyValues.size()) + " hashValues.size=" + to_string(oldHashValues.size()));

    // Call ReadTree with the new state root to get the hashes of the initial values of all read or written keys
    vector<HashValueGL> hashValues;
    zkr = pHashDB->readTree(batchNewStateRoot, keyValues, hashValues);
    zkassertpermanent(zkr==ZKR_SUCCESS);
    zklog.info("READ TREE batchNewStateRoot=" + fea2string(fr, batchNewStateRoot) + " keyValues.size=" + to_string(keyValues.size()) + " hashValues.size=" + to_string(hashValues.size()));

    TimerStopAndLog(HASHDB64_WORKFLOW_TEST);

    return 0;
}