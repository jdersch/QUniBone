/* filesystem_rt11.cpp - RT11 file system


  Copyright (c) 2022, Joerg Hoppe
  j_hoppe@t-online.de, www.retrocmp.com

  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  - Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

  - Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

  - Neither the name of the copyright holder nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  06-jan-2022 JH      created

 */
#include <string.h>
#include <inttypes.h> // PRI* formats
#include <fcntl.h>


#include "logger.hpp"

#include "blockcache_dec.hpp"
#include "filesystem_rt11.hpp"


namespace sharedfilesystem {


static const unsigned RT11_BLOCKSIZE = 512 ;
static const unsigned RT11_MAX_BLOCKCOUNT =  0x10000 ; // block addr only 16 bit
// no partitioned disks at the moment

static const unsigned RT11_FILE_EPRE  = 0000020	; // dir entry status word: file has prefix block(s)
static const unsigned RT11_FILE_ETENT = 0000400	; // dir entry status word: tentative file
static const unsigned RT11_FILE_EMPTY = 0001000	; // dir entry status word: empty area
static const unsigned RT11_FILE_EPERM = 0002000	; // dir entry status word: permanent file
static const unsigned RT11_DIR_EEOS   = 0004000	; // dir entry status word: end of segment marker
static const unsigned RT11_FILE_EREAD = 0040000	; // dir entry status word: write protect, deletion allowed
static const unsigned RT11_FILE_EPROT = 0100000	; // dir entry status word: protect permanent file

// pseudo file for volume parameters
#define RT11_VOLUMEINFO_BASENAME  "$VOLUM" // valid RT11 file name
#define RT11_VOLUMEINFO_EXT	      "INF"
// pseudo file for boot sector
#define RT11_BOOTBLOCK_BASENAME   "$BOOT" // valid RT11 file name "$BOOT.BLK"
#define RT11_BOOTBLOCK_EXT        "BLK"

// whatever is in blocks 2...5
#define RT11_MONITOR_BASENAME     "$MONI"
#define RT11_MONITOR_EXT          "TOR"

// mark data files with directory extension bytes and prefix blocks
// in the host filesystem with these extensions
// Example: data from host file "LOGGER.DAT.prefix" is put in the prefix block of
// file "LOGGER.DAT"
#define RT11_STREAMNAME_DIREXT	"dirext"
#define RT11_STREAMNAME_PREFIX	"prefix"



// copy of stream, but link to different file
rt11_stream_c::rt11_stream_c(file_rt11_c *_file, rt11_stream_c *stream):
    file_dec_stream_c(_file, stream->stream_name)
{
    file = _file ; // uplink
    host_path = stream->host_path ;
    init() ;
}

rt11_stream_c::rt11_stream_c(file_rt11_c *_file, string _stream_name):
    file_dec_stream_c(_file, _stream_name)
{
    file = _file ; // uplink
    init() ;
}

rt11_stream_c::~rt11_stream_c()
{
}


void rt11_stream_c::init()
{
    file_dec_stream_c::init() ;
    blocknr = 0;
    byte_offset = 0;
    changed = false ;
}

// construct the host path and filename
// MUST be the inverse of stream_by_host_filename()
// result is used to find host files in host map.
// make /dir1/dir2/filname.ext[.streamname]
string rt11_stream_c::get_host_path()
{
    // let host build the linux path, using my get_filename()
    // result is just "/filename"
    string result = filesystem_host_c::get_host_path(file) ;

    if (!stream_name.empty()) {
        result.append(".");
        result.append(stream_name);
    }

    return result;
}




file_rt11_c::file_rt11_c()
{
    stream_data = nullptr ;
    stream_dir_ext = nullptr ;
    stream_prefix = nullptr ;

    basename = "";
    ext = "";
    block_count = 0;
    internal = false;
}


// clone constructor. only metadata
// most derived class must select copy constructors of virtual base
// https://stackoverflow.com/questions/28450803/copy-constructor-is-not-inherited
file_rt11_c::file_rt11_c(file_rt11_c *f): file_base_c(f), file_dec_c(f)
//file_rt11_c::file_rt11_c(file_rt11_c *f) : file_base_c(static_cast<file_base_c*>(f)), file_dec_c(static_cast<file_dec_c*>(f))
{
    basename = f->basename ;
    ext = f->ext ;
    block_count = f->block_count ;
    internal = f->internal ;

    // clone streams, new file has same name, so streams get same host path
    stream_data = stream_dir_ext = stream_prefix = nullptr ;
}


file_rt11_c::~file_rt11_c()
{
    // streams[] are automatically destroyed
    if (stream_data)
        delete stream_data ;
    if (stream_dir_ext)
        delete stream_dir_ext ;
    if (stream_prefix)
        delete stream_prefix ;
}


// dir::copy_metadata_to() not polymorph .. limits of C++ or my wisdom
// instances for each filesystem, only rt11 and xxdp, not needed for host
void directory_rt11_c::copy_metadata_to(directory_base_c *_other_dir)
{
    auto other_dir = dynamic_cast<directory_rt11_c *>(_other_dir) ;
    // start condition: other_dir already updated ... recursive by clone-constructor
    assert(other_dir != nullptr) ;

    // directory recurse not necessary for RT11 ... but this may serve as template
    for(unsigned i = 0; i < subdirectories.size(); ++i) {
        auto subdir = dynamic_cast<directory_rt11_c *>(subdirectories[i]) ;
        // add to other filesystem a new copy-instance
        other_dir->filesystem->add_directory(other_dir, new directory_rt11_c(subdir));
    }
    for(unsigned i = 0; i < files.size(); ++i) {
        file_rt11_c * f  = dynamic_cast<file_rt11_c *>(files[i]) ;
        file_rt11_c * fnew = new file_rt11_c(f) ;
        other_dir->add_file(fnew) ;

        // add all the streams
        if (f->stream_data != nullptr)
            fnew->stream_data = new rt11_stream_c(fnew, f->stream_data) ;
        if (f->stream_dir_ext != nullptr)
            fnew->stream_dir_ext = new rt11_stream_c(fnew, f->stream_dir_ext) ;
        if (f->stream_prefix != nullptr)
            fnew->stream_prefix = new rt11_stream_c(fnew, f->stream_prefix) ;
    }
}


// BASENAME.EXT
string file_rt11_c::get_filename()
{
    return filesystem_rt11_c::make_filename(basename, ext) ;
}

rt11_stream_c **file_rt11_c::get_stream_ptr(string stream_code)
{
    // which stream?
    if (stream_code.empty())
        return &stream_data ;
    else if (!strcasecmp(stream_code.c_str(), RT11_STREAMNAME_DIREXT))
        return &stream_dir_ext ;
    else if (!strcasecmp(stream_code.c_str(), RT11_STREAMNAME_PREFIX))
        return &stream_prefix ;
    else
        return nullptr ;
}


// have file attributes or data content changed?
// filename not compared, speed!
// Writes to the image set the change flag
// this->has_changed(cmp) != cmp->has_changed(this)
bool file_rt11_c::data_changed(file_base_c *_cmp)
{
    auto cmp = dynamic_cast <file_rt11_c*>(_cmp) ;
    assert(cmp) ;

    // metadata_snapshot file has no data, and maybe used as "left operand"
    if (stream_data && stream_data->changed)
        return true ;

    return
//			basename.compare(cmp->basename) != 0
//			|| ext.compare(cmp->ext) != 0
        memcmp(&modification_time, &cmp->modification_time, sizeof(modification_time)) // faster
        //	|| mktime(modification_time) == mktime(cmp->modification_time)
        || readonly != cmp->readonly
        || file_size != cmp->file_size ;
}

// enumerate streams
unsigned file_rt11_c::get_stream_count()
{
    return 3 ;
}
file_dec_stream_c *file_rt11_c::get_stream(unsigned index)
{
    switch(index) {
    case 0:
        return stream_data ;
    case 1:
        return stream_dir_ext ;
    case 2:
        return stream_prefix ;
    default:
        return nullptr ;
    }
}




filesystem_rt11_c::filesystem_rt11_c(		drive_info_c _drive_info,
        storageimage_base_c *_image_partition, uint64_t _image_partition_size)
    : filesystem_dec_c(_drive_info, _image_partition, _image_partition_size)
{
    layout_info = get_documented_layout_info(_drive_info.drive_type) ;
    changed_blocks = new boolarray_c(layout_info.block_count)  ;

    // create root dir.
    add_directory(nullptr, new directory_rt11_c() ) ;
    assert(rootdir->filesystem == this) ;
//    rootdir = new directory_rt11_c() ; // simple list of files
//    rootdir->filesystem = this ;

    // sort order for files. For regexes the . must be escaped by \.
    // and a * is .*"
    //	reproduce test tape
    sort_group_regexes.reserve(10) ; // speed up, no relloc
    sort_add_group_pattern("RT11.*\\.SYS") ;
    sort_add_group_pattern("DD\\.SYS") ;
    sort_add_group_pattern("SWAP\\.SYS") ;
    sort_add_group_pattern("TT\\.SYS") ;
    sort_add_group_pattern("DL\\.SYS") ;
    sort_add_group_pattern("STARTS\\.COM") ;
    sort_add_group_pattern("DIR\\.SAV") ;
    sort_add_group_pattern("DUP\\.SAV") ;

    init() ;

}

filesystem_rt11_c::~filesystem_rt11_c()
{
    init() ; // free files

    delete changed_blocks ;
    changed_blocks = nullptr ; // signal to base class destructor
    delete rootdir ;
    rootdir = nullptr ; // signal to base class destructor
}



// free / clear all structures, set default values
void filesystem_rt11_c::init()
{
    // set device params

    // image may be variable sized !
    blockcount = needed_blocks(image_partition_size);

    /*
     blockcount = layout_info->block_count;
     */

    if (blockcount == 0)
        FATAL("rt11_filesystem_init(): RT-11 blockcount for device %s not yet defined!",
              drive_info.device_name.c_str());

    // trunc large devices, only 64K blocks addressable = 32MB
    // no support for partitioned disks at the moment
    assert(blockcount <= RT11_MAX_BLOCKCOUNT);

    bootblock_filename = make_filename(RT11_BOOTBLOCK_BASENAME, RT11_BOOTBLOCK_EXT) ;
    monitor_filename = make_filename(RT11_MONITOR_BASENAME, RT11_MONITOR_EXT) ;
    volume_info_filename = make_filename(RT11_VOLUMEINFO_BASENAME, RT11_VOLUMEINFO_EXT) ;

    clear_rootdir() ;

    // defaults for home block, according to [VFFM91], page 1-3
    pack_cluster_size = 1;
    first_dir_blocknr = 6;
    // system_version = "V3A"; RAD50: 0xa9, 0x8e
    system_version = "V05"; // RAD50: 0x53, 0x8e
    volume_id = "RT11A       ";
    owner_name = "            " ;
    system_id ="DECRT11A    " ;
    dir_entry_extra_bytes = 0;
    homeblock_chksum = 0;
    struct_changed = false ;
}



// copy filesystem, but without file content
// needed to get a snapshot for change compare
void filesystem_rt11_c::copy_metadata_to(filesystem_base_c *metadata_copy)
{
    auto _rootdir = dynamic_cast<directory_rt11_c *>(rootdir) ;
    _rootdir->copy_metadata_to(metadata_copy->rootdir) ;
}


// join basename and ext
// with "." on empty extension "FILE."
// used as key for file map
string filesystem_rt11_c::make_filename(string basename, string ext)
{
    string _basename = trim_copy(basename);
    string _ext = trim_copy(ext);

    if (_basename.empty())
        _basename = "_"; // at least the filename must be non-empty

    string result = _basename;
    if (!_ext.empty()) {
        result.append(".");
        result.append(_ext);
    }
    std::transform(result.begin(), result.end(),result.begin(), ::toupper);
    return result ;
}


/*
 * Filesystem parameter for specific drive.
 * AA-5279B-TC_RT-11_V4.0_System_Users_Guide_Mar80.pdf page 4-110
 * See AA-5279B-TC RT-11 V4.0 User Guide, "INITIALIZE", pp. 4-108..110
 * RK06/7 = 32 bad blocks, RL01/RL02 = 10
 *
 * also AA-PDU0A-TC_RT-11_Commands_Manual_Aug91.pdf "INITIALIZE", pp. 146
 *
 * Modified  by parse of actual disc image.
 */
filesystem_rt11_c::layout_info_t filesystem_rt11_c::get_documented_layout_info(enum dec_drive_type_e drive_type)
{
    layout_info_t result ;
    result.drive_type = drive_type ;
    result.block_size = 512 ; // for all drives
    result.first_dir_blocknr = 6; // for all drives
    switch (drive_type) {
    case devRK035:
        result.replacable_bad_blocks = 0 ;
        result.dir_seg_count = 16 ;
        break ;
    case devTU58:
        // result.block_count = 512 ;
        result.replacable_bad_blocks = 0 ;
        result.dir_seg_count = 1 ;
        break ;
    case devTU56:
        result.replacable_bad_blocks = 0 ;
        result.dir_seg_count = 1 ;
        break ;
    case devRF:
        result.replacable_bad_blocks = 0 ;
        result.dir_seg_count = 4 ;
        break ;
    case devRS:
        result.replacable_bad_blocks = 0 ;
        result.dir_seg_count = 4 ;
        break ;
    case devRP023:
        result.replacable_bad_blocks = 0 ;
        result.dir_seg_count = 31 ;
        break ;
    case devRX01:
        // result.block_count = 494 ; // todo
        result.replacable_bad_blocks = 0 ;
        result.dir_seg_count = 1 ;
        break ;
    case devRX02:
        // result.block_count = 988 ; // todo
        result.replacable_bad_blocks = 0 ;
        result.dir_seg_count = 4 ;
        break ;
    case devRK067:
        result.replacable_bad_blocks = 32 ;
        result.dir_seg_count = 31 ;
        break ;
    case devRL01:
        // result.block_count = 511*20 ; // without last track 10225 pyRT11
        result.dir_seg_count = 16 ;
        result.replacable_bad_blocks = 10 ;
        break ;
    case devRL02:
        // result.block_count = 1023*20 ;// 1023*20 without last track. 20465 pyRT11
//        result.dir_seg_count = 16 ;
        result.dir_seg_count = 31 ; // rt11 5.5 INIT
        result.replacable_bad_blocks = 10 ;
        break ;
    case devRX50:
        // result.dir_seg_count = 1 ; documented
        result.dir_seg_count = 4 ; // v5.3 INIT
        result.replacable_bad_blocks = 0 ;
        break ;
    case devRX33:
        // result.dir_seg_count = 1 ; documented
        result.dir_seg_count = 16 ; // v5.3 INIT
        result.replacable_bad_blocks = 0 ;
        break ;
    default:
        if (drive_info.mscp_block_count > 0) {
            // RT11 on big MSCP drives
            result.dir_seg_count = 31 ; // max.
            result.replacable_bad_blocks = 0 ;
        } else
            FATAL("storageimage_rt11_c::get_drive_info(): invalid drive") ;
    }
    result.block_count = drive_info.get_usable_capacity() / result.block_size ;

    return result ;
} ;


/*************************************************************
 * filesystem_rt11_c low level operators
 *************************************************************/

// ptr to first byte of block, inlines of filesystem_c
#define IMAGE_BLOCKNR2OFFSET(blocknr) (RT11_BLOCKSIZE *(blocknr))
// convert pointer in image to block
#define IMAGE_OFFSET2BLOCKNR(image_offset) ( (image_offset) / RT11_BLOCKSIZE)
// offset in block in bytes
#define IMAGE_OFFSET2BLOCKOFFSET(image_offset) ( (image_offset) % RT11_BLOCKSIZE)

// read block[start] ... block[start+blockcount-1] into data[]
void filesystem_rt11_c::stream_parse(rt11_stream_c *stream, rt11_blocknr_t start,
                                     uint32_t byte_offset, uint32_t data_size)
{
    stream->blocknr = start;
    stream->byte_offset = byte_offset;
    image_partition->get_bytes(stream, RT11_BLOCKSIZE * start + byte_offset, data_size) ;
    // stream not imported from host
    assert(stream->host_path.empty())  ;
    stream->host_path = stream->get_host_path() ;
}

// write stream to image
void filesystem_rt11_c::stream_render(rt11_stream_c *stream)
{
    stream->image_position = RT11_BLOCKSIZE * stream->blocknr + stream->byte_offset ;
    image_partition->set_bytes(stream) ;
}





// needed dir segments for given count of entries
// usable in 1 segment : 2 blocks - 5 header words
// entry size = 7 words + dir_entry_extra_bytes
unsigned filesystem_rt11_c::rt11_dir_entries_per_segment()
{
    // without extra bytes: 72 [VFFM91] 1-15
    int result = (2 * RT11_BLOCKSIZE - 2 * 5) / (2 * 7 + dir_entry_extra_bytes);

    // in a segment 3 entries spare, including end-of-segment
    assert(result > 3) ;
    result -= 3;
    return result;
}

unsigned filesystem_rt11_c::rt11_dir_needed_segments(unsigned _file_count)
{
    // without extra bytes: 72 [VFFM91] 1-15
    int entries_per_seg = rt11_dir_entries_per_segment();
    _file_count++; // one more for the mandatory "empty space" file entry
    // round up to whole segments
    return (_file_count + entries_per_seg - 1) / entries_per_seg;
}

// iterate all blocks of a file for change
void filesystem_rt11_c::calc_file_stream_change_flag(        rt11_stream_c *stream)
{
    rt11_blocknr_t blknr, blkend;
    if (!stream)
        return;
    stream->changed = false;
    if (changed_blocks) {
        blkend = stream->blocknr + needed_blocks(stream->size());
        for (blknr = stream->blocknr; !stream->changed && blknr < blkend; blknr++)
            stream->changed |= BOOLARRAY_BIT_GET(changed_blocks, blknr);
//		stream->changed |= boolarray_bit_get(image_changed_blocks, blknr);
        // possible optimization: boolarray is tested sequentially
    }
}

void filesystem_rt11_c::calc_file_change_flags()
{
    file_rt11_c *f ;

    if (changed_blocks == nullptr)
        return;

    // Homeblock changed?
    struct_changed = BOOLARRAY_BIT_GET(changed_blocks, 1);

    // any dir entries changed?
    for (rt11_blocknr_t blknr = first_dir_blocknr;
            blknr < first_dir_blocknr + 2 * dir_total_seg_num; blknr++)
        struct_changed |= BOOLARRAY_BIT_GET(changed_blocks, blknr);

    // volume info changed?
    f = dynamic_cast<file_rt11_c *>(file_by_path.get(volume_info_filename)) ;
    if (f && struct_changed)
        f->stream_data->changed = true ;

    // calc_file_stream_change_flag(monitor);
    for (unsigned i = 0; i < file_count(); i++) {
        f = file_get(i);
        calc_file_stream_change_flag(f->stream_prefix);
        calc_file_stream_change_flag(f->stream_data); // also internal
    }


}


// calculate ratio between directory segments and data blocks
// Pre: files filled in
// output:
//	used_blocks
//  dir_total_seg_num
//	dir_max_seg_nr
// 2 Modi:
// a) test_data_size == 0: calc on base of file[], change file system
// b) test_data_size > 0: check wether file of length "test_data_size" would fit onto
//	the existing volume
void filesystem_rt11_c::rt11_filesystem_calc_block_use(unsigned test_data_size)
{
    unsigned _dir_max_seg_nr;
    unsigned _used_file_blocks;
    unsigned _available_blocks;

    if (dir_entry_extra_bytes > 16)
        FATAL("Extra bytes in directory %d is > 16 ... how much is allowed?", dir_entry_extra_bytes);

    // 1) calc segments & blocks needed for existing files
    _used_file_blocks = 0;
    dir_file_count = 0 ;
    for (unsigned i = 0; i < file_count(); i++)
        if (!file_get(i)->internal) {
            // round file sizes up to blocks
            // prefix size and data size already sum'd up to file->block_count
            _used_file_blocks += file_get(i)->block_count;
            dir_file_count++ ;
        }
    if (test_data_size)
        _used_file_blocks += needed_blocks(test_data_size);

    // total blocks available for dir and data
    // On disk supporting STd144 bad sector info,
    // "available blocks" should not be calculated from total disk size,
    // but from usable blockcount of "layout_info".
    // Difficulties in case of enlarged images!
    _available_blocks = blockcount - first_dir_blocknr; // boot, home, 2..5 used
    if (test_data_size)
        _dir_max_seg_nr = rt11_dir_needed_segments(dir_file_count + 1);
    else
        _dir_max_seg_nr = rt11_dir_needed_segments(dir_file_count);
    if (_available_blocks < _used_file_blocks + 2 * _dir_max_seg_nr) {
        // files do not fit on volume
        if (!test_data_size)
            free_blocks = 0; // can't be negative
        throw filesystem_exception("rt11_filesystem_calc_block_use(): FILESYSTEM OVERFLOW");
    }
    if (test_data_size)
        return ;

    /* end of test mode */
    // now modify file system
    dir_max_seg_nr = _dir_max_seg_nr;
    used_file_blocks = _used_file_blocks;

    free_blocks = _available_blocks - _used_file_blocks - 2 * _dir_max_seg_nr;
    // now used_blocks,free_blocks, dir_total_seg_num valid.

    /* Plan use of remaining free_space
     * how many files would be allocated in the remaining free space?
     * derive from average file size, but allow 1.5 * as much
     * Most critical test situations:
     * All dir segments full, and only 2 block in file area left.
     * assigned these blocks to 1 more file would need a new dir segment,
     * which would need these 2 blocks too.
     * If 3 blocks are left: 2 can be used for additional dir segment,
     * and 1 for new file.
     *
     * Problem with adaptive # of directory segments:
     * If PDP is writing many more files it will run into dir entry limit.
     * So do not go below default for this disk drive.
     */
    dir_total_seg_num = layout_info.dir_seg_count ; // default is lower limit

    if (dir_file_count == 0) {
        // if disk empty: start with only 1 segment
        dir_max_seg_nr = 1;
    } else {
        unsigned planned_avg_file_blocks; // average filesize in blocks
        unsigned planned_new_file_count; // planned count of additional files
        unsigned planned_used_file_blocks; // planned block requirement for all files (existing + planned)
        unsigned planned_dir_total_seg_num; // planned dir segment requirement for all files (existing + planned)
        planned_avg_file_blocks = used_file_blocks / dir_file_count;
        if (planned_avg_file_blocks < 1)
            planned_avg_file_blocks = 1;
        // 1st estimate for possible new files.
        // Assume they have average size.
        // too big, since additional dir segments reduce free space
        planned_new_file_count = free_blocks / planned_avg_file_blocks + 1;
        // reduce amount of planned files, until files+dir fit on disk
        do {
            planned_new_file_count--;
            planned_used_file_blocks = used_file_blocks + planned_new_file_count * planned_avg_file_blocks;
            // plan for 50% more file count
            planned_dir_total_seg_num = rt11_dir_needed_segments(dir_file_count + (planned_new_file_count * 3) / 2);
        } while (planned_new_file_count
                 && _available_blocks < planned_used_file_blocks + 2 * planned_dir_total_seg_num);
        // solution found: save
        if (planned_dir_total_seg_num > 31)
            planned_dir_total_seg_num = 31;
        if (planned_dir_total_seg_num > dir_total_seg_num)
            dir_total_seg_num = planned_dir_total_seg_num; // enlarge upto 31
    }

    // calculate free blocks again
    assert(_available_blocks >= used_file_blocks + (unsigned)2 * dir_total_seg_num);
    free_blocks = _available_blocks - used_file_blocks
                  - 2 * dir_total_seg_num;
}


/**************************************************************
 * parse()
 * convert byte array of image into logical objects
 **************************************************************/

// parse filesystem special blocks to file
void filesystem_rt11_c::parse_internal_blocks_to_file(string _basename, string _ext,
        uint32_t start_block_nr, uint32_t data_size)
{
    string fname = make_filename(_basename, _ext) ;
    file_base_c* fbase = file_by_path.get(fname);
    file_rt11_c* f = dynamic_cast<file_rt11_c*>(fbase);
//    file_rt11_c* f = dynamic_cast<file_rt11_c*>(file_by_path(fname));

    assert(f == nullptr) ;
    f = new file_rt11_c(); // later own by rootdir
    f->internal = true ;
    f->basename = _basename ;
    f->ext = _ext ;
    f->block_nr = start_block_nr;
    f->block_count = needed_blocks(data_size);
    f->readonly = true ;
    rootdir->add_file(f); //  before parse-stream

    f->stream_data = new rt11_stream_c(f, "");
    stream_parse(f->stream_data, f->block_nr, 0, f->block_count * RT11_BLOCKSIZE);
    f->file_size = f->stream_data->size() ;
}


void filesystem_rt11_c::parse_homeblock()
{
    uint16_t w;
    uint8_t *s ;
    int i;
    int sum;

    // work on chache
    block_cache_dec_c cache(this) ;
    cache.load_from_image(1, 1) ; // work on block 1
    // bad block bitmap not needed

    // INIT/RESTORE area: ignore
    // BUP ignored

    pack_cluster_size = cache.get_image_word_at(1, 0722);
    first_dir_blocknr = cache.get_image_word_at(1, 0724);
    if (first_dir_blocknr != 6)
        throw filesystem_exception("parse_homeblock(): first_dir_blocknr expected 6, is %d", first_dir_blocknr);
    first_dir_blocknr = cache.get_image_word_at(1, 0724);
    w = cache.get_image_word_at(1, 0726);
    system_version = rad50_decode(w);
    // 12 char volume id. V3A, or V05, ...
    char buffer[13] ;
    s = cache.get_image_addr(1, 0730) ;
    strncpy(buffer, (char *)s, 12);
    buffer[12] = 0;
    volume_id = string(buffer) ;
    // 12 char owner name
    s = cache.get_image_addr(1, 0744) ;
    strncpy(buffer, (char *)s, 12);
    buffer[12] = 0;
    owner_name = string(buffer) ;
    // 12 char system id
    s = cache.get_image_addr(1, 0760) ;
    strncpy(buffer, (char *)s, 12);
    buffer[12] = 0;
    system_id = string(buffer) ;
    homeblock_chksum = cache.get_image_word_at(1, 0776);
    // verify checksum. But found a RT-11 which writes 0000 here?
    for (sum = i = 0; i < 0776; i += 2)
        sum += cache.get_image_word_at(1, i);
    sum &= 0xffff;
    /*
     if (sum != homeblock_chksum)
     fprintf(flog, "Home block checksum error: is 0x%x, expected 0x%x\n", sum,
     (int) homeblock_chksum);
     */
}

// point to start of  directory segment [i]
// segment[0] at directory_startblock (6), and 1 segment = 2 blocks. i starts with 1.
//#define DIR_SEGMENT(i)  ( (uint16_t *) (image_partition_data + first_dir_blocknr*RT11_BLOCKSIZE +(((i)-1)*2*RT11_BLOCKSIZE)) )
// absolute position in image
#define DIR_SEGMENT_BLOCK_NR(i)  (first_dir_blocknr +(((i)-1)*2))

void filesystem_rt11_c::parse_directory()
{
    uint32_t ds_offset; // byte offset in image of directory segment begin
    uint32_t ds_nr = 0; // runs from 1
    uint32_t ds_next_nr = 0;
    uint32_t w;
    uint16_t de_data_blocknr; // start blocknumber for file data

    uint32_t de_offset; // directory entry in current directory segment
    uint16_t de_nr; // directory entry_nr, runs from 0
    uint16_t de_len; // total size of directory entry in bytes
    // by this segment begins.

    /*** iterate directory segments ***/
    used_file_blocks = 0;
    free_blocks = 0;
    block_cache_dec_c cache(this) ;
    ds_nr = 1;
    cache.load_from_image(DIR_SEGMENT_BLOCK_NR(ds_nr), 2) ; // init with 1st seg = 2 blocks
    do {
        // DEC WORD # : 1	2	3	4	5	6	7  8
        // Byte offset: 0	2	4	6	8  10  12  14
        ds_offset = DIR_SEGMENT_BLOCK_NR(ds_nr) * RT11_BLOCKSIZE ;
        // read 5 word directory segment header
        w = cache.get_image_word_at(ds_offset + 0) ; // word #1 total num of segments
        if (ds_nr == 1)
            dir_total_seg_num = w;
        else if (w != dir_total_seg_num)
            throw filesystem_exception("parse_directory(): ds_header_total_seg_num in entry %d different from entry 1", ds_nr);
        if (ds_nr == 1)
            dir_max_seg_nr = cache.get_image_word_at(ds_offset + 4); // word #3
        ds_next_nr = cache.get_image_word_at(ds_offset + 2); // word #2 nr of next segment
        if (ds_next_nr > dir_max_seg_nr)
            throw filesystem_exception("parse_directory(): next segment nr %d > maximum %d", ds_next_nr, dir_max_seg_nr);
        de_data_blocknr = cache.get_image_word_at(ds_offset + 8); // word #5 block of data start
        if (ds_nr == 1) {
            dir_entry_extra_bytes = cache.get_image_word_at(ds_offset + 6); // word #4: extra bytes
            file_space_blocknr = de_data_blocknr; // 1st dir entry
        }
        //

        /*** iterate directory entries in segment ***/
        de_len = 14 + dir_entry_extra_bytes ;
        de_nr = 0;
        de_offset = ds_offset + 10; // 1st entry 5 words after segment start
        while (!(cache.get_image_word_at(de_offset) & RT11_DIR_EEOS)) { // end of segment?
            uint16_t de_status = cache.get_image_word_at(de_offset); // word #1 status
            if (de_status & RT11_FILE_EMPTY) { // skip empty entries
                w = cache.get_image_word_at(de_offset + 8); // word #5: file len
                free_blocks += w;
            } else if (de_status & RT11_FILE_EPERM) { // only permanent files
                // new file! read dir entry
                file_rt11_c *f = new file_rt11_c(); // later own by rootdir
                f->status = de_status;
                // basename and ext WITHOUT leading spaces
                string s ;
                // basename: 6 chars
                w = cache.get_image_word_at(de_offset + 2); // word #2
                s.assign(rad50_decode(w)) ;
                w = cache.get_image_word_at(de_offset + 4); // word #3
                s.append(rad50_decode(w)) ;
                f->basename = rtrim_copy(s) ; // " EMPTY.FIL" has leading space
                // extension: 3 chars
                w = cache.get_image_word_at(de_offset + 6); // word #4
                s.assign(rad50_decode(w)) ;
                f->ext = rtrim_copy(s) ;

                // blocks in data stream
                f->block_nr = de_data_blocknr; // startblock on disk
                f->block_count = cache.get_image_word_at(de_offset + 8); // word #5 file len
                used_file_blocks += f->block_count;
                // fprintf(stderr, "parse %s.%s, %d blocks @ %d\n", f->basename, f->ext,	f->block_count, f->block_nr);
                // ignore job/channel
                // creation date
                w = cache.get_image_word_at(de_offset + 12); // word #7
                // 5 bit year, 2 bit "age". Year since 1972
                // date "0" is possible, then no display in DIR output
                if (w) {
                    f->modification_time.tm_year = 72 + (w & 0x1f) + 32 * ((w >> 14) & 3);
                    f->modification_time.tm_mday = (w >> 5) & 0x1f;
                    f->modification_time.tm_mon = ((w >> 10) & 0x0f) - 1;
                } else { // oldest: 1-jan-72
                    f->modification_time.tm_year = 72;
                    f->modification_time.tm_mday = 1;
                    f->modification_time.tm_mon = 0;
                }
                // "readonly", if either EREAD or EPROT)
                f->readonly = false;
                if (f->status & (RT11_FILE_EREAD))
                    f->readonly = true;
                if (f->status & (RT11_FILE_EPROT))
                    f->readonly = true;
                rootdir->add_file(f); //save, now owned by dir

                // Extract extra bytes in directory entry as stream ...
                if (dir_entry_extra_bytes) {
                    assert(f->stream_dir_ext == nullptr);
                    f->stream_dir_ext = new rt11_stream_c(f, RT11_STREAMNAME_DIREXT);
                    stream_parse(f->stream_dir_ext, // word #8 extra words
                                 /*start block*/IMAGE_OFFSET2BLOCKNR(de_offset + 14),
                                 /* byte_offset*/IMAGE_OFFSET2BLOCKOFFSET(de_offset + 14),
                                 dir_entry_extra_bytes);
                    // generate only a stream if any bytes set <> 00
                    if (f->stream_dir_ext->is_zero_data(0)) {
                        delete f->stream_dir_ext;
                        f->stream_dir_ext = nullptr;
                    }
                }

            }

            // advance file start block in data area, also for empty entries
            de_data_blocknr += cache.get_image_word_at(de_offset + 8); // word 4 total file len

            // next dir entry
            de_nr++;
            de_offset += de_len;
            if ( (unsigned)(de_offset - ds_offset) > 2 * RT11_BLOCKSIZE) // 1 segment = 2 blocks
                throw filesystem_exception("parse_directory(): list of entries exceeds %d bytes", 2 * RT11_BLOCKSIZE);
        }

        // next segment, 2 blocks into cache
        ds_nr = ds_next_nr;
        cache.load_from_image(DIR_SEGMENT_BLOCK_NR(ds_nr), 2) ;
    } while (ds_nr > 0);
}

// parse prefix and data blocks
// not using an block cache necessary, because large sequential reads
void filesystem_rt11_c::parse_file_data()
{
    rt11_blocknr_t prefix_block_count;

    for (unsigned i = 0; i < file_count(); i++) {
        file_rt11_c *f = file_get(i);
        if (f->internal)
            continue ;
        // fprintf(stderr, "%d %s.%s\n", i, f->basename, f->ext) ;
        // data area may have "prefix" block.
        // format not mandatory, use DEC recommendation
        if (f->status & RT11_FILE_EPRE) {
            block_cache_dec_c cache(this) ; // only to read 1st byte in prefix block
            cache.load_from_image(f->block_nr, 1) ;
            prefix_block_count = *cache.get_image_addr(f->block_nr, 0);// first byte in block
            // DEC: low byte of first word = blockcount
            assert(f->stream_prefix == nullptr);

            f->stream_prefix = new rt11_stream_c(f, RT11_STREAMNAME_PREFIX);
            // stream is everything behind first word
            stream_parse(f->stream_prefix, f->block_nr, 2, prefix_block_count * RT11_BLOCKSIZE - 2);
        } else
            prefix_block_count = 0;

        // after prefix: remaining blocks are data
        assert(f->stream_data == nullptr);
        f->stream_data = new rt11_stream_c(f, "");
        stream_parse(f->stream_data, f->block_nr + prefix_block_count, 0,
                     (f->block_count - prefix_block_count) * RT11_BLOCKSIZE);
        f->file_size = f->stream_data->size() ;
    }
}

// fill the pseudo file with textual volume information
void filesystem_rt11_c::parse_volumeinfo()
{
    file_rt11_c* fout = dynamic_cast<file_rt11_c*>(file_by_path.get(volume_info_filename));
    if (fout == nullptr) {
        fout = new file_rt11_c(); // later owned by rootdir
        fout->internal = true ;
        fout->basename = RT11_VOLUMEINFO_BASENAME ;
        fout->ext  = RT11_VOLUMEINFO_EXT ;
        fout->block_nr = 0; // not needed
        fout->block_count = 0 ;
        fout->readonly = true ;

        rootdir->add_file(fout); // before stream creation

        // other properties from init()
        fout->stream_data = new rt11_stream_c(fout, "");
        fout->stream_data->host_path = fout->stream_data->get_host_path() ;
    }

    // volume info is synthetic, maps not from disk area
    // so own buffer.  freed by ~file_dec_stream_c()
    string text_buffer ; // may grow large, but data kept on heap

    // 1. generate the data
    char line[1024];

    sprintf(line, "# %s - info about RT-11 volume on %s device.\n",
            volume_info_filename.c_str(), drive_info.device_name.c_str());
    text_buffer.append(line) ;

    time_t t = time(NULL); // now
    struct tm tm = *localtime(&t);
    fout->modification_time = tm ;
    sprintf(line, "# Produced by QUniBone at %d-%d-%d %d:%d:%d\n", tm.tm_year + 1900,
            tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    text_buffer.append(line);

    sprintf(line, "\npack_cluster_size=%d\n", pack_cluster_size);
    text_buffer.append(line);

    sprintf(line, "\n# Block number of first directory segment\nfirst_dir_blocknr=%d\n",
            first_dir_blocknr);
    text_buffer.append(line);

    sprintf(line, "\nsystem_version=%s\n", system_version.c_str());
    text_buffer.append(line);

    sprintf(line, "\nvolume_id=%s\n", volume_id.c_str());
    text_buffer.append(line);

    sprintf(line, "\nowner_name=%s\n", owner_name.c_str());
    text_buffer.append(line);

    sprintf(line, "\nsystem_id=%s\n", system_id.c_str());
    text_buffer.append(line);

    sprintf(line, "\n# number of %d byte blocks on volume\nblock_count=%d\n",
            RT11_BLOCKSIZE, blockcount);
    text_buffer.append(line);

    sprintf(line, "\n# number of extra bytes per directory entry\ndir_entry_extra_bytes=%d\n",
            dir_entry_extra_bytes);
    text_buffer.append(line);

    sprintf(line, "\n# Total number of segments in this directory (can hold %d files) \n"
            "dir_total_seg_num=%d\n",
            rt11_dir_entries_per_segment() * dir_total_seg_num, dir_total_seg_num);
    text_buffer.append(line);

    sprintf(line, "\n# Number of highest dir segment in use\ndir_max_seg_nr=%d\n",
            dir_max_seg_nr);
    text_buffer.append(line);

    sprintf(line, "\n# Start block of file area = %d\n", file_space_blocknr);
    text_buffer.append(line);

    unsigned dir_file_no = 0 ; // count only files in directory, not internals
    for (unsigned i = 0; i < file_count(); i++) {
        file_rt11_c *f = file_get(i);
        if (f->internal)
            continue ;
        sprintf(line, "\n# File %2d \"%s\".", dir_file_no, f->get_filename().c_str());
        text_buffer.append(line);
        if (f->stream_prefix) {
            sprintf(line, " Prefix %u = 0x%x bytes, start block %d @ 0x%X.",
                    f->stream_prefix->size(), f->stream_prefix->size(), f->stream_prefix->blocknr,
                    f->stream_prefix->blocknr * RT11_BLOCKSIZE);
            text_buffer.append(line);
        } else
            text_buffer.append(" No prefix.");
        if (f->stream_data) {
            sprintf(line, " Data %u = 0x%x bytes, start block %d @ 0x%X.", f->stream_data->size(),
                    f->stream_data->size(), f->stream_data->blocknr, f->stream_data->blocknr * RT11_BLOCKSIZE);
            text_buffer.append(line);
        } else
            text_buffer.append(" No data.");
        dir_file_no++ ;
    }

    text_buffer.append("\n");

    // ?? assert(text_buffer.size() < fout->stream_data->size()) ;
    fout->stream_data->set(&text_buffer) ;
    fout->file_size = fout->stream_data->size() ;

    // VOLUM INF is "changed", if home block or directories changed
    fout->stream_data->changed = struct_changed;
}


// analyse the image, build filesystem data structure
// parameters already set by _reset()
// In case of invalid image or minor error
// 	throw filesystem_dec_exception,
//  file tree always valid, defective objects deleted
void filesystem_rt11_c::parse()
{
    std::exception_ptr eptr;

    // events in the queue references streams, which get invalid on re-parse.
    assert(event_queue.empty()) ;

    init();
    try {

        parse_internal_blocks_to_file(RT11_BOOTBLOCK_BASENAME, RT11_BOOTBLOCK_EXT, 0, RT11_BLOCKSIZE) ;
        parse_internal_blocks_to_file(RT11_MONITOR_BASENAME, RT11_MONITOR_EXT, 2, 4 * RT11_BLOCKSIZE) ;

        parse_homeblock() ;
        parse_directory() ;

        parse_file_data();
    }
    catch (filesystem_exception &e) {
        eptr = std::current_exception() ;
    }
    // in case of error: still cleanup
    // rt11_filesystem_print_diag(stderr);

    // mark file->data , ->prefix as changed, for changed image blocks
    calc_file_change_flags();

    //  data now stable, generate internal volume info text last
    parse_volumeinfo() ;

    if (eptr)
        std::rethrow_exception(eptr);
}




/**************************************************************
 * render
 * create an binary image from logical datas structure
 **************************************************************/


// calculate blocklists for monitor, bitmap,mfd, ufd and files
// total blockcount may be enlarged
// Pre: files filled in
void filesystem_rt11_c::rt11_filesystem_layout()
{
    int file_start_blocknr;

    rt11_filesystem_calc_block_use(0) ; // throws

    // free, used blocks, dir_total_seg_num now set

    // file area begins after directory segment list
    file_start_blocknr = first_dir_blocknr + 2 * dir_total_seg_num;
    file_space_blocknr = file_start_blocknr;
    dir_file_count=0 ;
    for (unsigned i = 0; i < file_count(); i++) {
        file_rt11_c *f = file_get(i);
        if (f->internal)
            continue ;
        dir_file_count++ ; // count # of files in directory
        f->block_nr = file_start_blocknr;
        // set start of prefix and data
        if (f->stream_prefix) {
            f->stream_prefix->blocknr = file_start_blocknr;
            // prefix needs 1 extra word for blockcount
            f->stream_prefix->byte_offset = 2;
            file_start_blocknr += needed_blocks(f->stream_prefix->size() + 2);
        }
        if (f->stream_data) {
            f->stream_data->blocknr = file_start_blocknr;
            file_start_blocknr += needed_blocks(f->stream_data->size());
        }
        // f->block_count set in file_stream_add()
        assert(file_start_blocknr - f->block_nr == f->block_count);
    }
    // save begin of free space for _render()
    render_free_space_blocknr = file_start_blocknr;

}

/*
// write moni.tor und boot.block files to image
void filesystem_rt11_c::render_internal_blocks_from_file(file_rt11_c *f,
			uint32_t start_block_nr, uint32_t data_size)
	{
		rt11_stream_c *stream = f->stream_data ;
		stream->blocknr = start_block_nr ;
		stream->byte_offset = 0 ;
		stream->set_size(data_size) ;
		stream_render(stream) ;
		return ERROR_OK;
}
**/

void filesystem_rt11_c::render_homeblock()
{
//    uint8_t *homeblk = IMAGE_BLOCKNR2OFFSET(1);
    uint16_t w;
    int i, sum;

    block_cache_dec_c cache(this) ;
    cache.init(1, 1) ; // home block is #1
    // write the bad block replacement table
    // no idea about it, took from TU58 and RL02 image and from Don North
    cache.set_image_word_at(1, 0, 0000000);
    cache.set_image_word_at(1, 2, 0170000);
    cache.set_image_word_at(1, 4, 0007777);

    // rest until 0203 was found to be 0x43 (RL02) or 0x00 ?

    // INITIALIZE/RESTORE data area 0204-0251 == 0x084-0xa9
    // leave blank

    // BUP information area 0252-0273 == 0xaa-0xbb found as 00's

    // "reserved for Digital"
    cache.set_image_word_at(1, 0700, 0177777); // from v5.5 INIT

    cache.set_image_word_at(1, 0722, pack_cluster_size);
    cache.set_image_word_at(1, 0724, first_dir_blocknr);

    w = rad50_encode(system_version);
    cache.set_image_word_at(1, 0726, w);

    // 12 char volume id. V3A, or V05, ...

    uint8_t *s ;
    s = cache.get_image_addr(1, 0730) ;
    // always 12 chars long, right padded with spaces
    string tmp = volume_id ;
    strcpy((char *)s, tmp.append(12-tmp.length(), ' ').c_str()) ;

    // 12 char owner name
    s = cache.get_image_addr(1, 0744);
    tmp = owner_name ;
    strcpy((char *)s, tmp.append(12-tmp.length(), ' ').c_str()) ;

    // 12 char system id
    s = cache.get_image_addr(1, 0760) ;
    tmp = system_id ;
    strcpy((char *)s, tmp.append(12-tmp.length(), ' ').c_str()) ;

    // build checksum over all words
    for (sum = i = 0; i < 0776; i += 2)
        sum += cache.get_image_word_at(1, i);
    sum &= 0xffff;
    homeblock_chksum = sum;
    cache.set_image_word_at(1, 0776, sum);

    cache.flush_to_image() ;
}

// write file f into segment ds_nr and entry de_nr
// if f = NULL: write free chain entry
// must be called with ascending de_nr
void filesystem_rt11_c::render_directory_entry(block_cache_dec_c &cache, file_rt11_c *f, int ds_nr, int de_nr)
{
    uint32_t ds_offset ;// byte offset in image of directory segment begin
    uint32_t de_offset; // ptr to dir entry in image
    int dir_entry_word_count = 7 + (dir_entry_extra_bytes / 2);
    uint16_t w;
    char buff[80];

    // DEC WORD # : 1	2	3	4	5	6	7  8
    // Byte offset: 0	2	4	6	8  10  12  14
    ds_offset =	DIR_SEGMENT_BLOCK_NR(ds_nr) * RT11_BLOCKSIZE;
    if (de_nr == 0) {
        // 1st entry in segment: write 5 word header
        cache.set_image_word_at(ds_offset + 0,dir_total_seg_num); // word #1
        if (ds_nr == dir_max_seg_nr)
            cache.set_image_word_at(ds_offset + 2, 0); // word #2: next segment
        else
            cache.set_image_word_at(ds_offset + 2, ds_nr + 1); // link to next segment
        cache.set_image_word_at(ds_offset + 4, dir_max_seg_nr); // word #3
        cache.set_image_word_at(ds_offset + 6, dir_entry_extra_bytes); //word #4
        if (f)
            cache.set_image_word_at(ds_offset + 8, f->block_nr); // word #5 start of first file on disk
        else
            // end marker at first entry
            cache.set_image_word_at(ds_offset + 8, file_space_blocknr);
    }
    // write dir_entry
    de_offset = ds_offset + 10 + de_nr * 2 * dir_entry_word_count;
    //fprintf(stderr, "ds_nr=%d, de_nr=%d, ds in img=0x%lx, de in img =0x%lx\n", ds_nr, de_nr,
    //		(uint8_t*) ds - *image_partition_data_ptr, (uint8_t*) de - *image_partition_data_ptr);
    if (f == nullptr) {
        // write start of free chain: space after last file
        cache.set_image_word_at(de_offset + 0, RT11_FILE_EMPTY);
        // after INIT free space has the name " EMPTY.FIL"
        cache.set_image_word_at(de_offset + 2, rad50_encode(" EM"));
        cache.set_image_word_at(de_offset + 4, rad50_encode("PTY"));
        cache.set_image_word_at(de_offset + 6, rad50_encode("FIL"));
        cache.set_image_word_at(de_offset + 8, free_blocks); // word #5 file len
        cache.set_image_word_at(de_offset + 10, 0); // word #6 job/channel
        cache.set_image_word_at(de_offset + 12, 0); // word # 7 INIT sets a creation date ... don't need to!
    } else {
        // regular file
        // status
        w = RT11_FILE_EPERM;
        if (f->readonly)
//            w |= RT11_FILE_EREAD | RT11_FILE_EPROT; // E.READ: Monitor-Dir-Inv ?
            w |= RT11_FILE_EPROT;
        if (f->stream_prefix)
            w |= RT11_FILE_EPRE;
        cache.set_image_word_at(de_offset + 0, w);

        // filename chars 0..2
        strncpy(buff, f->basename.c_str(), 3);
        buff[3] = 0;
        cache.set_image_word_at(de_offset + 2, rad50_encode(buff));

        // filename chars 3..5. trailing spaces added by rad50_encode()
        if (strlen(f->basename.c_str()) < 4)
            buff[0] = 0;
        else
            strncpy(buff, f->basename.c_str() + 3, 3);
        buff[3] = 0;
        cache.set_image_word_at(de_offset + 4, rad50_encode(buff));
        // ext
        cache.set_image_word_at(de_offset + 6, rad50_encode(f->ext));
        // total file len
        cache.set_image_word_at(de_offset + 8, f->block_count); // word #5 file len
        // clr job/channel
        cache.set_image_word_at(de_offset + 10, 0); // word #6 job
        //date. do not set "age", as it is not evaluated by DEC software.
        // year already in range 1972..1999
        w = f->modification_time.tm_year - 72;
        w |= f->modification_time.tm_mday << 5;
        w |= (f->modification_time.tm_mon + 1) << 10;
        cache.set_image_word_at(de_offset + 12, w); // word #7 creation date
        if (f->stream_dir_ext) {
            // write bytes from "dir extension" stream into directory entry
            if (f->stream_dir_ext->size() > dir_entry_extra_bytes)
                throw filesystem_exception("render_directory(): file %s dir_ext size %d > extra bytes in dir %d\n",
                                           f->get_filename().c_str(), f->stream_dir_ext->size(), dir_entry_extra_bytes);
            cache.set_image_bytes_at(de_offset+14, f->stream_dir_ext) ;
        }
    }
    // write end-of-segment marker behind dir_entry.
    cache.set_image_word_at(de_offset + 2*dir_entry_word_count, RT11_DIR_EEOS);
    // this is overwritten by next entry; and remains if last entry in segment
}

// Pre: all files are arrange as gap-less stream, with only empty segment
// after last file.
void filesystem_rt11_c::render_directory()
{
    block_cache_dec_c cache(this) ;
    // cache holds all directory segemtns, allocated in ascending blocks

    unsigned dir_entries_per_segment = rt11_dir_entries_per_segment(); // cache
    int ds_nr = 1; // # of segment,starts with 1
    int de_nr; // # of entry

    cache.init(DIR_SEGMENT_BLOCK_NR(ds_nr), 2) ; // 1st seg = 2 blocks
    unsigned dir_file_no = 0 ; // count only files in directory, not internals
    for (unsigned i = 0; i < file_count(); i++) {
        file_rt11_c *f = file_get(i);
        if (f->internal)
            continue ;
        // which segment?
        int next_ds_nr = (dir_file_no / dir_entries_per_segment) + 1; // runs from 1
        // which entry in the segment?
        de_nr = dir_file_no % dir_entries_per_segment; // runs from 0
        if (next_ds_nr != ds_nr) { // move cache to next dir segment
            cache.flush_to_image() ;
            ds_nr = next_ds_nr ;
            cache.init(DIR_SEGMENT_BLOCK_NR(ds_nr), 2) ; // next seg = 2 blocks
        }
        render_directory_entry(cache, f, ds_nr, de_nr);
        dir_file_no++ ;
    }
    // last entry: start of empty free chain
    int next_ds_nr = dir_file_count / dir_entries_per_segment + 1;
    de_nr = dir_file_count % dir_entries_per_segment;
    if (next_ds_nr != ds_nr) { // move cache to next dir segment
        cache.flush_to_image() ;
        ds_nr = next_ds_nr ;
        cache.init(DIR_SEGMENT_BLOCK_NR(ds_nr), 2) ; // next seg = 2 blocks
    }
    render_directory_entry(cache, nullptr, ds_nr, de_nr);

    cache.flush_to_image() ;

}

// write user file data into image
void filesystem_rt11_c::render_file_data()
{
    for (unsigned i = 0; i < file_count(); i++) {
        file_rt11_c *f = file_get(i);
        if (f->internal)
            continue ;
        if (f->stream_prefix) { 		// prefix block?
            block_cache_dec_c cache(this) ;// just to write the prefix block count word
            cache.load_from_image(f->stream_prefix->blocknr, 1) ;
            // low byte of 1st word on volume is blockcount,
            uint16_t prefix_block_count = needed_blocks(f->stream_prefix->size() + 2);
            if (prefix_block_count > 255)
                FATAL("Render: Prefix of file \"%s\" = %d blocks, maximum 255",
                      f->get_filename().c_str(), prefix_block_count);

            cache.set_image_word_at(f->stream_prefix->blocknr, 0, prefix_block_count);
            // start block and byte offset 2 already set by layout()
            cache.flush_to_image() ;
            stream_render(f->stream_prefix); // loads and saves again
        }
        if (f->stream_data != nullptr)
            stream_render(f->stream_data);
    }
}


// write filesystem into image
// Assumes all file data and blocklists are valid
// return: 0 = OK
void filesystem_rt11_c::render()
{
    // is there an efficient way to clear to probably huge image?
    // Else previous written stuff remains in unused blocks.
    // format media, all 0's
    rt11_filesystem_layout() ; // throws

    // write boot block and monitor, if file exist
    file_rt11_c* bootblock = dynamic_cast<file_rt11_c*>(file_by_path.get(bootblock_filename));
    if (bootblock) {
        bootblock->stream_data->blocknr = 0;
        bootblock->stream_data->byte_offset = 0 ;
        if (bootblock->stream_data->size() != RT11_BLOCKSIZE)
            throw filesystem_exception("bootblock has illegal size of %d bytes.", bootblock->stream_data->size());
        stream_render(bootblock->stream_data);
    } else
        image_partition->set_zero(0, RT11_BLOCKSIZE) ; // clear area
    file_rt11_c* monitor = dynamic_cast<file_rt11_c*>(file_by_path.get(monitor_filename));
    if (monitor) {
        monitor->stream_data->blocknr = 2; // 2...5
        monitor->stream_data->byte_offset = 0 ;
        if (monitor->stream_data->size() > 4 * RT11_BLOCKSIZE)
            throw filesystem_exception("monitor has illegal size of %d bytes.", monitor->stream_data->size());
        stream_render(monitor->stream_data);
    } else
        image_partition->set_zero(2 * RT11_BLOCKSIZE, 4 * RT11_BLOCKSIZE) ; // clear area

    render_homeblock();
    render_directory();
    render_file_data();

    parse_volumeinfo() ;
}



/**************************************************************
 * FileAPI
 * add / get files in logical data structure
 **************************************************************/


// take a file of the shared dir, push it to the filesystem
// a RT11 file can have several streams, "streamname" is
// the host file is only one stream of a PDP filesystem file
// special:
// -1: bootblock
// -2 : boot monitor
// -3: volume information text file
// fname: basename.ext
//	Optional some of the home block data could be hold in a textfile
// with "name=value" entries, and a name of perhaps "$META.DAT"
// This would include a "BOOT=file name entry" for the "INIT/BOOT" monitor.
// A RT-11 file may consist of three different datastreams
// 1. file data, as usual
// 2. dat in a special "prefix" block
// 3. data in extended directory entries.
// if "hostfname" ends with  RT11_FILENAME_DIREXT_extension,
//	it is interpreted to contain data for directory extension
// if "hostfname" ends with  RT11_FILENAME_PREFIX_extension,
//	it is interpreted to contain data for the "prefix" blocks
//
// result: -1  = volume overflow

/*
	finds file and stream for a given host file.
	Als parse host_filename into components
	May or may not exist, may be special filesystem area
	result: false, when hostfile is a reserved file not to be imported
		($VOLUME.INF)
	result_file	result_stream
	null		null			file is new
	ptr			null			its a new stream for an existing file
	ptr			ptr				existing stream of an existing file
*/

bool filesystem_rt11_c::stream_by_host_filename(string host_fname,
        file_rt11_c **result_file, string *result_host_filename,
        rt11_stream_c **result_stream, string *result_stream_code)
{
    *result_file = nullptr;
    *result_stream = nullptr ;

    // one of 3 streams of a regular or internal file
    // process host file name
    string host_ext, stream_code ="" ; // "", "dirext", ...
    split_path(host_fname, nullptr, nullptr, nullptr, &host_ext) ;
    // is outer extension a known streamname?
    if (!strcasecmp(host_ext.c_str(), RT11_STREAMNAME_DIREXT)
            || !strcasecmp(host_ext.c_str(), RT11_STREAMNAME_PREFIX))  {
        stream_code = host_ext ;
        // now strip stream_code from host_fname
        split_path(host_fname, nullptr, nullptr, &host_fname, nullptr);
    }
    *result_host_filename = host_fname ;
    *result_stream_code = stream_code ;

    // make filename.extension to "FILN.E"(not: "FILN  .E  ")
    // find file with this name.
    string _basename, _ext;
    filename_from_host(&host_fname, &_basename, &_ext);
    string filename = make_filename(_basename, _ext) ;
    auto f = *result_file = dynamic_cast<file_rt11_c *>(file_by_path.get(filename)) ; // already hashed?

    if (f)
        *result_stream = *(f->get_stream_ptr(stream_code)) ;
    else
        assert(*result_stream == nullptr) ; // file=null + stream not allowed
    return true ;
}


void filesystem_rt11_c::import_host_file(file_host_c *host_file)
{
    file_rt11_c *f ;
    string host_fname ;// host file name with out stream extension
    rt11_stream_c *stream ;
    string stream_code ; // clipped stream extension from host file name
    bool block_ack_event = true ; // do not feed changes back to host
    // false: changes are re-sent to the host. necessary for files like VOLUME.INF,
    // which change independently and whose changes must sent to the host.



    // RT11 has no subdirectories, so it accepts only plain host files from the rootdir
    // report file $VOLUME INFO not be read back
    if (dynamic_cast<directory_host_c*>(host_file) != nullptr)
        return ; // host directory
    if (host_file->parentdir == nullptr)
        return ;  // host root directory
    if (host_file->parentdir->parentdir != nullptr)
        return ; // file in host root subdirectory

    // locate stream and file, and/or produce RT11 names
    stream_by_host_filename(host_file->get_filename(), &f, &host_fname, &stream, &stream_code) ;

    string _basename, _ext;
    filename_from_host(&host_fname, &_basename, &_ext);
    // create event for existing file/stream? Is acknowledge from host, ignore.
    if (f != nullptr || stream != nullptr) {
        DEBUG(printf_to_cstr("RT11: Ignore \"create\" event for existing filename/stream %s.%s %s",
                             _basename.c_str(), _ext.c_str(), stream_code.c_str()));
        return ;
    }


    host_file->data_open(/*write*/ false) ;

    bool internal = false ;
    if (_basename == RT11_BOOTBLOCK_BASENAME && _ext == RT11_BOOTBLOCK_EXT) {
        internal = true ;
        if (host_file->file_size != RT11_BLOCKSIZE)
            throw filesystem_exception("Boot block not %d bytes", RT11_BLOCKSIZE);
    } else if (_basename == RT11_MONITOR_BASENAME && _ext == RT11_MONITOR_EXT) {
        internal = true ;
        if (host_file->file_size > 4 * RT11_BLOCKSIZE)
            throw filesystem_exception("Monitor block too big, has %d bytes, max %d", host_file->file_size,
                                       4 * RT11_BLOCKSIZE);
    } else if (_basename == RT11_VOLUMEINFO_BASENAME && _ext == RT11_VOLUMEINFO_EXT) {
        block_ack_event = false ;
        internal = true ;
    }

    // one of 3 streams of a regular file or data stream of internal

    assert(f == nullptr) ;
    assert(stream == nullptr) ;

    // check wether a new user file of "data_size" bytes would fit onto volume
    // recalc filesystem parameters
    try {
        rt11_filesystem_calc_block_use(internal ? 0 : host_file->file_size) ;
    } catch (filesystem_exception &e) {
        throw filesystem_exception("Disk full, file \"%s\" with %d bytes too large", host_fname.c_str(), host_file->file_size);
    }
    // new file
    f = new file_rt11_c();
    f->basename = _basename ;
    f->ext = _ext;
    f->internal = internal ;

    f->modification_time = host_file->modification_time;
    // only range 1972..1999 allowed
    if (f->modification_time.tm_year < 72)
        f->modification_time.tm_year = 72;
    else if (f->modification_time.tm_year > 99)
        f->modification_time.tm_year = 99;
    f->readonly = false; // set from data stream
    rootdir->add_file(f) ; // now owned by rootdir, in map, path valid

    //2. create correct stream
    rt11_stream_c **stream_ptr = f->get_stream_ptr(stream_code) ;
    if (stream_ptr == &f->stream_data) {
        assert(f->stream_data == nullptr) ;
        // file is readonly, if data stream has no user write permission (see stat(2))
        f->readonly = host_file->readonly;
    } else if (stream_ptr == &f->stream_dir_ext) {
        assert(f->stream_dir_ext == nullptr) ;
        // size of dir entry extra bytes is largest dir_ext stream
        if (host_file->file_size > dir_entry_extra_bytes)
            dir_entry_extra_bytes = host_file->file_size;
    } else if (stream_ptr == &f->stream_prefix) {
        assert(f->stream_prefix == nullptr) ;
    } else
        throw filesystem_exception("Illegal stream code %s", stream_code.c_str());

    // allocate and fill the stream. to block
    *stream_ptr = new rt11_stream_c(f, stream_code);
    (*stream_ptr)->host_path = host_file->path ;
    (*stream_ptr)->set(&host_file->data, host_file->file_size) ;


    // calc size and blocks count = prefix +data
    f->block_count = 0;
    if (f->stream_prefix) { // internals only data
        f->block_count += needed_blocks(f->stream_prefix->size() + 2); // 2 bytes length word
    }
    if (f->stream_data) {
        // file size is just data stream size rounded to blocks
        f->file_size = get_block_size() * needed_blocks(f->stream_data->size());
        f->block_count += needed_blocks(f->stream_data->size());
    }

    host_file->data_close() ;

    if (block_ack_event)
        ack_event_filter.add(host_file->path) ;

}


void filesystem_rt11_c::delete_host_file(string host_path)
{
    // build RT11 name and stream code
    string host_dir, host_fname ;

    split_path(host_path, &host_dir, &host_fname, nullptr, nullptr) ;
    if (host_dir != "/")
        // ignore stuff from host subdirectories
        return ;
    file_rt11_c *f ;
    rt11_stream_c *stream ;
    string stream_code ; // clipped stream extension from host file name

    // locate stream and file, and/or produce RT11 names
    if (!stream_by_host_filename(host_fname, &f, &host_fname, &stream, &stream_code))
        return ; // name rejected


    // delete stream, must exist
    if (stream == nullptr) {
        DEBUG(printf_to_cstr("RT11: ignore \"delete\" event for missing stream %s of file %s.", stream_code.c_str(), host_fname.c_str()));
        return ;
    }

    if (f == nullptr) {
        DEBUG(printf_to_cstr("RT11: ignore \"delete\" event for missing file %s.", host_fname.c_str()));
        return ;
    }

    //
    string _basename, _ext;
    filename_from_host(&host_fname, &_basename, &_ext);
    if (_basename == RT11_VOLUMEINFO_BASENAME && _ext == RT11_VOLUMEINFO_EXT) {
        return ; // do not change from host -> change evetns not blocked via ack_event
    }


    // one of 3 streams of a regular or internal file:
    if (stream == f->stream_data) {
        delete(f->stream_data) ;
        f->stream_data= nullptr ;
    } else if (stream == f->stream_dir_ext) {
        delete(f->stream_dir_ext) ;
        f->stream_dir_ext= nullptr ;
    } else if (stream == f->stream_prefix) {
        delete(f->stream_prefix) ;
        f->stream_prefix= nullptr ;
    }
    // delete file on last stream
    if (f->stream_data == nullptr
            && f->stream_dir_ext == nullptr
            && f->stream_prefix == nullptr)
        rootdir->remove_file(f) ;

    ack_event_filter.add(host_path) ;

}


file_rt11_c *filesystem_rt11_c::file_get(int fileidx)
{
    file_rt11_c *f ;
    if (fileidx >= 0 && fileidx < (int)file_count()) {
        // regular file. Must've been added with rootdir->add_file()
        f = dynamic_cast< file_rt11_c *> (rootdir->files[fileidx]) ;
        assert(f) ;
    } else
        return nullptr; // not a valid file idx

    return f ;
}


/* convert filenames and timestamps */


// result ist basename.ext, without spaces
// "filname" and "ext" contain components WITH spaces, if != NULL
// "bla.foo.c" => "BLA.FO", "C  ", result = "BLA.FO.C"
// "bla" => "BLA."
string filesystem_rt11_c::filename_from_host(string *hostfname, string *result_basename, string *result_ext)
{
    string pathbuff = *hostfname ;


    // upcase and replace forbidden characters
    for (unsigned i = 0 ; i < pathbuff.length() ; i++) {
        char c ;
        switch(c = pathbuff[i]) {
        case '_' :
            c = ' ' ;
            break ;
        case 'a' ... 'z':
            c = toupper(c) ;
            break ;
        case 'A' ... 'Z':
        case '$':
        case '.':
        case '0' ... '9':
            c = c ;
            break ;
        default:
            c = '%' ;
        }
        pathbuff[i] = c ;
    }


    // make it 6.3. can use Linux function
    string _basename, _ext ;
    split_path(pathbuff, nullptr, nullptr, &_basename, &_ext) ;
    _ext = _ext.substr(0, 3) ;
    trim(_ext) ;
    _basename = _basename.substr(0, 6) ;
    trim(_basename) ;

    if (result_basename != nullptr)
        *result_basename = _basename;
    if (result_ext != nullptr)
        *result_ext = _ext;

    // with "." on empty extension "FILE."
    return make_filename(_basename, _ext);
}



// sort files in rootdir according to order,
// set by "sort_add_group_pattern()"
void filesystem_rt11_c::sort()
{
    // non recursive
    filesystem_base_c::sort(rootdir->files) ;
}


/**************************************************************
 * Display structures
 **************************************************************/

string filesystem_rt11_c::rt11_date_text(struct tm t)
{
    string mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov",
                     "Dec"
                   };
    char buff[80];
    sprintf(buff, "%02d-%3s-%02d", t.tm_mday, mon[t.tm_mon].c_str(), t.tm_year);
    return string(buff);
}
// print a DIR like RT11
// RT11SJ.SYS    79P 20-Dec-85      DD    .SYS     5  20-Dec-85
// SWAP  .SYS    27  20-Dec-85      TT    .SYS     2  20-Dec-85
// DL    .SYS     4  20-Dec-85      STARTS.COM     1  20-Dec-85
// DIR   .SAV    19  20-Dec-85      DUP   .SAV    47  20-Dec-85
//  8 Files, 184 Blocks
//  320 Free blocks

string filesystem_rt11_c::rt11_dir_entry_text(file_rt11_c *f)
{
    char buff[80];
    sprintf(buff, "%6s.%-3s%6d%c %s", f->basename.c_str(), f->ext.c_str(), f->block_count,
            f->readonly ? 'P' : ' ', rt11_date_text(f->modification_time).c_str());
    return string(buff);
}


void filesystem_rt11_c::print_dir(FILE *stream)
{
    string line;
    // no header
    line = "";
    unsigned file_nr = 0 ;
    for (unsigned i = 0; i < file_count(); i++) {
        file_rt11_c *f = file_get(i) ;
        if (f->internal)
            continue ;
        if (file_nr & 1) {
            // odd file #: right column, print
            line.append("		");
            line.append(rt11_dir_entry_text(f));
            fprintf(stream, "%s\n", line.c_str());
            line = "";
        } else {
            // even: left column
            line.assign(rt11_dir_entry_text(f));
        }
        file_nr++ ;
    }
    if (! line.empty()) // print pending left column
        fprintf(stream, "%s\n", line.c_str());
    fprintf(stream, " %d files, %d blocks\n", file_count(), used_file_blocks);
    fprintf(stream, " %d Free blocks\n", free_blocks);

}


void filesystem_rt11_c::print_diag(FILE *stream)
{
    print_diag(stream);
}

} // namespace


