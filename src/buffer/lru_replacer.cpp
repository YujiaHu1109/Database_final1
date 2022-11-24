/**
 * LRU implementation
 */

#include "buffer/lru_replacer.h"
#include "page/page.h"

#include <cassert>

namespace scudb 
{

    template <typename T> LRUReplacer<T>::LRUReplacer() : size(0) 
	{
        head = new node();
        tail = head;
    }

    template <typename T> LRUReplacer<T>::~LRUReplacer() = default;
    /*
     * Insert value into LRU
     */
    template <typename T> void LRUReplacer<T>::Insert(const T &value) 
	{
        std::lock_guard<std::mutex> lock(mutexLock);

        auto it = map.find(value);
        if(it != map.end()) // find it
        {
        	node *pre = it->second->pre;
            node *cur = pre->next;
            pre->next = std::move(cur->next);
            pre->next->pre = pre;

            cur->pre = tail;
            tail->next = std::move(cur);
            tail = tail->next;
		}
		else if(it == map.end())
		{
			tail->next = new node(value, tail);
            tail = tail->next;
            map.emplace(value, tail);
            size++;
		}
    }

    /* If LRU is non-empty, pop the head member from LRU to argument "value", and
     * return true. If LRU is empty, return false
     */
    template <typename T> bool LRUReplacer<T>::Victim(T &value) {
        std::lock_guard<std::mutex> lock(mutexLock);

        if(size == 0) // If LRU is empty, return false
		{
            return false;
        }

        value = head->next->data;
        head->next = head->next->next; // pop the head member from LRU to argument "value"
        if(head->next != nullptr) 
		{
            head->next->pre = head;
        }

        map.erase(value);
        if(--size == 0) 
		{
            tail = head;
        }
        return true;
    }

    /*
     * Remove value from LRU. If removal is successful, return true, otherwise
     * return false
     */
    template <typename T> bool LRUReplacer<T>::Erase(const T &value) 
	{
        std::lock_guard<std::mutex> lock(mutexLock);

        auto it = map.find(value);
        if(it != map.end()) 
		{
            if(it->second != tail) 
			{
                node *pre = it->second->pre;
                node *cur = pre->next;
                pre->next = std::move(cur->next);
                pre->next->pre = pre;
            } 
			else 
			{
                tail = tail->pre;
                delete tail->next;
            }

            map.erase(value);
            if(--size == 0)  tail = head;
            return true;
        }
        return false;
    }

    template <typename T> size_t LRUReplacer<T>::Size() 
	{
        std::lock_guard<std::mutex> lock(mutexLock);
        return map.size();
    }


    template class LRUReplacer<Page *>;
    // test only
    template class LRUReplacer<int>;

} // namespace scudb
