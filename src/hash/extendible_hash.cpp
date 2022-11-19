#include <list>

#include "hash/extendible_hash.h"
#include "page/page.h"

namespace scudb {

/*
 * constructor
 * array_size: fixed array size for each bucket
 */
    template <typename K, typename V>
    ExtendibleHash<K, V>::ExtendibleHash(size_t size): bucketSize(size), bucketCount(0), pairCount(0), globalDepth(0) 
	{
        buckets.emplace_back(new Bucket(0, 0));
        bucketCount = 1;
    }

/*
 * helper function to calculate the hashing address of input key
 */
    template <typename K, typename V>
    size_t ExtendibleHash<K, V>::HashKey(const K &key) 
	{
        return std::hash<K>()(key);
    }

/*
 * helper function to return global depth of hash table
 * NOTE: you must implement this function in order to pass test
 */
    template <typename K, typename V>
    int ExtendibleHash<K, V>::GetGlobalDepth() const 
	{
        std::lock_guard<std::mutex> lock(mutexLock);
        return globalDepth;
    }

/*
 * helper function to return local depth of one specific bucket
 * NOTE: you must implement this function in order to pass test
 */
    template <typename K, typename V>
    int ExtendibleHash<K, V>::GetLocalDepth(int bucket_id) const 
	{
        std::lock_guard<std::mutex> lock(mutexLock);
        if(buckets[bucket_id]) 
		{
            return buckets[bucket_id]->localDepth;
        }
        return -1;
    }

/*
 * helper function to return current number of bucket in hash table
 */
    template <typename K, typename V>
    int ExtendibleHash<K, V>::GetNumBuckets() const 
	{
        std::lock_guard<std::mutex> lock(mutexLock);
        return bucketCount;
    }


/*
 * lookup function to find value associate with input key
 */
    template <typename K, typename V>
    bool ExtendibleHash<K, V>::Find(const K &key, V &value) 
	{
        std::lock_guard<std::mutex> lock(mutexLock);
        size_t idx = HashKey(key) & ((1 << globalDepth) - 1);
        if(buckets[idx]) 
		{
            if(buckets[idx]->hashItems.find(key) != buckets[idx]->hashItems.end()) 
			{
                value = buckets[idx]->hashItems[key];
                return true;
            }
        }
        return false; // not find
    }

/*
 * delete <key,value> entry in hash table
 * Shrink & Combination is not required for this project
 */
    template <typename K, typename V>
    bool ExtendibleHash<K, V>::Remove(const K &key) 
	{
        std::lock_guard<std::mutex> lock(mutexLock);
        size_t idx = HashKey(key) & ((1 << globalDepth) - 1); // get idx
        size_t cnt = 0;

        if(buckets[idx]) 
		{
            auto tmpBucket = buckets[idx]; 
            cnt = tmpBucket->hashItems.erase(key);
            pairCount -= cnt; // reduce pair count
        }
        if (cnt != 0) return true; // successfully remove the item
		else return false; // remove no item
    }

/*
 * insert <key,value> entry in hash table
 * Split & Redistribute bucket when there is overflow and if necessary increase
 * global depth
 */
    template <typename K, typename V>
    void ExtendibleHash<K, V>::Insert(const K &key, const V &value) 
	{
        std::lock_guard<std::mutex> lock(mutexLock);
        size_t bucket_id = HashKey(key) & ((1 << globalDepth) - 1);// get the low bits
        // find the place to insert, new a bucket if empty
        if(buckets[bucket_id] == nullptr) 
		{
            buckets[bucket_id] = std::make_shared<Bucket>(bucket_id, globalDepth); // dynamic memory
            bucketCount++;
        }

        auto tmpBucket = buckets[bucket_id];
		// cover the previous value
        if(tmpBucket->hashItems.find(key) != tmpBucket->hashItems.end()) 
		{
            tmpBucket->hashItems[key] = value;
            return;
        }
        // temporary insert key&value into tmpBucket
        tmpBucket->hashItems.insert({key, value}); 
        pairCount++;
		// split the bucket and reallocate the key&value
        if(tmpBucket->hashItems.size() > bucketSize) 
		{
            auto oldIndex = tmpBucket->id; // old index
            auto oldDepth = tmpBucket->localDepth; // old localDepth
            std::shared_ptr<Bucket> newBucket = split(tmpBucket);

            if(newBucket == nullptr) // cannot split: overflow
			{
                tmpBucket->localDepth = oldDepth;
                return;
            }
            
            if(tmpBucket->localDepth > globalDepth) // expand the bucket array if localDepth > globalDepth
			{
                auto size = buckets.size();
                auto factor = (1 << (tmpBucket->localDepth - globalDepth));//3,2 -> 2

                globalDepth = tmpBucket->localDepth; // expand the globalDepth
                buckets.resize(buckets.size()*factor);
         
                buckets[tmpBucket->id] = tmpBucket;
                buckets[newBucket->id] = newBucket;
                
                for(size_t i = 0; i < size; i++) 
				{
                    if(buckets[i]) 
					{
                        if(i < buckets[i]->id)
						{
                            buckets[i].reset(); // release the space occupied by the inside pointer
                        } 
						else 
						{
                            auto step = 1 << buckets[i]->localDepth;
                            for(size_t j = i + step; j < buckets.size(); j += step) 
							{
                                buckets[j] = buckets[i];
                            }
                        }
                    }
                }
            } 
			else 
			{
                for (size_t i = oldIndex; i < buckets.size(); i += (1 << oldDepth)) 
				{
                    buckets[i].reset();
                }

                buckets[tmpBucket->id] = tmpBucket;
                buckets[newBucket->id] = newBucket;

                auto step = 1 << tmpBucket->localDepth;
                for (size_t i = tmpBucket->id + step; i < buckets.size(); i += step) 
				{
                    buckets[i] = tmpBucket;
                }
                for (size_t i = newBucket->id + step; i < buckets.size(); i += step) 
				{
                    buckets[i] = newBucket;
                }
            }
        }
    }

    template <typename K, typename V>
    std::shared_ptr<typename ExtendibleHash<K, V>::Bucket>
    ExtendibleHash<K, V>::split(std::shared_ptr<Bucket> &b) 
	{
        
        auto res = std::make_shared<Bucket>(0, b->localDepth);
        
        while(res->hashItems.empty()) 
		{
           
            b->localDepth++;
            res->localDepth++;
            
            for(auto it = b->hashItems.begin(); it != b->hashItems.end();) 
			{
                if (HashKey(it->first) & (1 << (b->localDepth - 1))) 
				{
                    res->hashItems.insert(*it);
                    res->id = HashKey(it->first) & ((1 << b->localDepth) - 1);
                    it = b->hashItems.erase(it);
                } 
				else 
				{
                    ++it;
                }
            }

            if(b->hashItems.empty()) 
			{
                b->hashItems.swap(res->hashItems);
                b->id = res->id;
            }
        }

        bucketCount++;
        return res;
    }

    template class ExtendibleHash<page_id_t, Page *>;
    template class ExtendibleHash<Page *, std::list<Page *>::iterator>;
// test purpose
    template class ExtendibleHash<int, std::string>;
    template class ExtendibleHash<int, std::list<int>::iterator>;
    template class ExtendibleHash<int, int>;
} // namespace scudb
