### Buffer Pool Manager:

## FrameHeader:

```
friend class BufferPoolManager;
friend class ReadPageGuard;
friend class WritePageGuard;
```

Private Fields:
```
const frame_id_t frame_id_;
std::shared_mutex rw_latch_; //shared mutext
std::atmoic<size_t> pin_count_;
bool is_dirty_;
std::vector<char> data_; //ptr point to the data of the page that this frame is holding
```

## BufferPoolManager:

```
private fields:
size_t num_frames_;
std::atomic<page_id_t> next_page_id_; // next padge id to be allocated, atomic to prevent race condition
std::share_ptr<std::mutex> bpm_latch_; // what does this protect (what internal data structure?)
std::vector<std::shared_ptr<FrameHeader>> frames_; // all frames that this bpm manages
std::unordered_map<page_id_t, frame_id_t> page_table_; // page table
std::list<frame_id_t> free_frames_; // free frames that do not hold any page data
std::shared_ptr<ArcReplacer> replacer_; // arc
std::shared_ptr<DiskScheduler> disk_scheduler_; // shared w page guards for flushing

// recommend to have a helper function that returns the frame ID that is free and has nothing stored within.
```

```
public methods
...
```

## ReadPageGuard

- Mutiple threads can share read access to the page's data. If any ReadPageGuard exists for the page, meaning there is no mutating for the page's data.

```
friend class BufferPoolManager
```

```
private fields:

page_id_t page_id_; // page that this is guarding
std::share_ptr<FrameHeader> frame_; // ptr of frame that holding the current page
std::share_ptr<std::mutex> bpm_latch_; // shared pointer to the buffer pool latch. Bufferpool don't know when this ReadPageGuard gets destructred, maintain a ptr to the pool's latch for when we need top update the frame's eviction state in the buffer pool replacer
std::shared_ptr<DiskSchedule> disk_scheduler_; // shared w bpm's disk scheduler, used when flushing page to disk
is_valid_{false};
```
## WritePageGuard:

- Similar to ReadPageGuard. Only one thread can have exclusive ownership over the page at once. Meaning, if WritePageGuard exists then no other WritePageGuard and ReadPageGuard can exist.

## DiskScheduler:

- Read/Write request for the DiskManager to execute.

```
struct DiskRequest {
bool is_write_;
char *data; // pointer to the start of memory location data
page_id_t page_id_;
std::promise<bool> callback_;

private fields:
Channel<std::optional<DiskRequest>> request_queue+; // shared queue to concurrently schedule and process requests
std::optional<std::thread> background_thread_; // responsible for issuing scheduled requests to the disk manager
}
```

## ArcReplacer:

```

```

```
FrameStatus {
    page_id_;
    frame_id_;
    evictable_;
    ArcStatus arc_status_; (MRU, MFU, MRU_GHOST, MFU_GHOST)
}

ArcReplacer {
    DISALLOW_COPY_AND_MOVE

    private:
        mru_, mfu_, mru_ghost_, mfu_ghost_;
        alive_map_, ghost_map_;
}
```

## DiskManager:

- Takes care of allocation and deallocation of pages within database
- Performs reading and writing of pages to and from disk, providing logical file layer
- Use lazy allocation, it only allocates space on disk when it is first acessed.
- Maintain a mapping of `page_id` to their corresponding offsets in the db file.

```
public:
    virtual void WritePage(page_id_t page_id, const char *page_data);
    virtual void ReadPage(page_id_t page_id, char *page_data);

private:
    std::fstream log_io_;
    std::filesystem::path log_file_name_;

    std::fstream db_io_;
    std::filesystem:: path db_file_name;

    std::unordered_map<page_id_t, size_t> pages; // page_id and offset mapping
    std::vector<size_t> free_slots_;

    std::mutex db_io_latch_; // protect file access
```
