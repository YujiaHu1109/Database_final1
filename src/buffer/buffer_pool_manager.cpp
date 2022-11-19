#include "buffer/buffer_pool_manager.h"

namespace scudb {

/*
 * BufferPoolManager Constructor
 * When log_manager is nullptr, logging is disabled (for test purpose)
 * WARNING: Do Not Edit This Function
 */
    BufferPoolManager::BufferPoolManager(size_t pool_size,
                                         DiskManager *disk_manager,
                                         LogManager *log_manager)
            : poolSize(pool_size), diskManager(disk_manager),
              logManager(log_manager)
    {
        // a consecutive memory space for buffer pool
        pages = new Page[poolSize];
        freeList = new std::list<Page *>;

        replacer = new LRUReplacer<Page *>;
        pageTable = new ExtendibleHash<page_id_t, Page *>(BUCKET_SIZE);

        // put all the pages into free list
        for (size_t i = 0; i < poolSize; ++i)
        {
            freeList->push_back(&pages[i]);
        }
    }

/*
 * BufferPoolManager Destructor
 * WARNING: Do Not Edit This Function
 */
    BufferPoolManager::~BufferPoolManager()
    {
        delete[] pages;
        delete pageTable;
        delete replacer;
        delete freeList;
    }

 /**
 * 1. search hash table.
 *  1.1 if exist, pin the page and return immediately
 *  1.2 if no exist, find a replacement entry from either free list or lru
 *      replacer. (NOTE: always find from free list first)
 * 2. If the entry chosen for replacement is dirty, write it back to disk.
 * 3. Delete the entry for the old page from the hash table and insert an
 * entry for the new page.
 * 4. Update page metadata, read page content from disk file and return page
 * pointer
 *
 * This function must mark the Page as pinned and remove its entry from LRUReplacer before it is returned to the caller.
 */
    Page *BufferPoolManager::FetchPage(page_id_t page_id)
    {
    	std::lock_guard<std::mutex> lock(mutexLock);
  		Page *tar = nullptr;
  		//1.1 if exist, pin the page and return immediately
		if (pageTable->Find(page_id,tar)) 
		{ 
			tar->pin_count_++; // mark the Page as pinned
    		replacer->Erase(tar); // remove its entry from LRUReplacer
    		return tar;
  		}
  		
  		//1.2 if no exist, find a replacement entry from either free list or lru
//  		tar = GetVictimPage();
//  		if (tar == nullptr) return tar;
		if(!freeList->empty())
        {
            tar = freeList->front();
            freeList->pop_front();
        }
        else
        {
            if(!replacer->Victim(tar))
            {
                return nullptr;
            }
        }
  		
  		//2 If the entry chosen for replacement is dirty, write it back to disk
  		if (tar->is_dirty_) 
  		{
    		diskManager->WritePage(tar->page_id_, tar->GetData());
  		}

  		//3 Delete the entry for the old page from the hash table and insert an entry for the new page
  		pageTable->Remove(tar->page_id_); // delete the entry for old page
  		pageTable->Insert(page_id,tar); // insert an entry for the new page
  		
  		//4 Update page metadata, read page content from disk file and return page pointer
  		diskManager->ReadPage(page_id,tar->data_); // initial meta data
  		tar->page_id_= page_id;
		tar->is_dirty_ = false;
		tar->pin_count_ = 1;
  		return tar;
	}

/*
 * Implementation of unpin page
 * if pin_count>0, decrement it and if it becomes zero, put it back to
 * replacer if pin_count<=0 before this call, return false. is_dirty: set the
 * dirty flag of this page
 */
    bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty)
    {
        std::lock_guard<std::mutex> lock(mutexLock);

        Page *tar = nullptr;
        pageTable->Find(page_id,tar);
        
        if (!pageTable->Find(page_id, tar))  return false;
    
        tar->is_dirty_ = is_dirty;
        if (tar->pin_count_ <= 0)  return false;
  		
        if (--tar->pin_count_ == 0)
        {
            replacer->Insert(tar);
        }
        return true;
    }

/*
 * Used to flush a particular page of the buffer pool to disk. Should call the
 * write_page method of the disk manager
 * if page is not found in page table, return false
 * NOTE: make sure page_id != INVALID_PAGE_ID
 */
    bool BufferPoolManager::FlushPage(page_id_t page_id)
    {
        std::lock_guard<std::mutex> lock(mutexLock);

        if (page_id == INVALID_PAGE_ID)  return false;

        Page *tar = nullptr;
        if (pageTable->Find(page_id, tar))
        {
            diskManager->WritePage(page_id, tar->GetData());
            return true;
        }
        return false;
    }

/**
 * User should call this method for deleting a page. This routine will call
 * disk manager to deallocate the page. First, if page is found within page
 * table, buffer pool manager should be responsible for removing this entry out
 * of page table, resetting page metadata and adding back to free list. Second,
 * call disk manager's DeallocatePage() method to delete from disk file. If
 * the page is found within page table, but pin_count != 0, return false
 */
    bool BufferPoolManager::DeletePage(page_id_t page_id)
    {
        std::lock_guard<std::mutex> lock(mutexLock);

        Page *tar = nullptr;
        if(pageTable->Find(page_id, tar))
        {
            if (tar->GetPinCount() > 0)  return false;

			pageTable->Remove(page_id);
			tar->is_dirty_ = false;
			tar->ResetMemory();
            freeList->push_back(tar);
    	}
    
		diskManager->DeallocatePage(page_id);
  		return true;  
    }

/**
 * User should call this method if needs to create a new page. This routine
 * will call disk manager to allocate a page.
 * Buffer pool manager should be responsible to choose a victim page either
 * from free list or lru replacer(NOTE: always choose from free list first),
 * update new page's metadata, zero out memory and add corresponding entry
 * into page table. return nullptr if all the pages in pool are pinned
 */
    Page *BufferPoolManager::NewPage(page_id_t &page_id)
    {
        std::lock_guard<std::mutex> lock(mutexLock);
        Page *tar = nullptr;
        if(!freeList->empty())
        {
            tar = freeList->front();
            freeList->pop_front();
        }
        else
        {
            if(!replacer->Victim(tar))
            {
                return nullptr;
            }
        }
//        tar = GetVictimPage();
//        if (tar == nullptr) return tar;

        page_id = diskManager->AllocatePage();
        if(tar->is_dirty_)
        {
            diskManager->WritePage(tar->page_id_, tar->GetData());
        }

        pageTable->Remove(tar->page_id_);

        pageTable->Insert(page_id, tar);

        tar->page_id_ = page_id;
  		tar->ResetMemory();
  		tar->is_dirty_ = false;
  		tar->pin_count_ = 1;

        return tar;
    }
} // namespace scudb
