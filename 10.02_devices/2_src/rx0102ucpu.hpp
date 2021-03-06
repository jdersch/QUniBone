/* rx0102ucpu.hpp: implementation of microCPU in dual RX01/RX02 disk drive case

 Copyright (c) 2020, Joerg Hoppe
 j_hoppe@t-online.de, www.retrocmp.com

 Permission is hereby granted, free of charge, to any person obtaining a
 copy of this software and associated documentation files (the "Software"),
 to deal in the Software without restriction, including without limitation
 the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 JOERG HOPPE BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 10-jan-2020  JH      begin

 The microCPU board contains all logic and state for the RX01/02 subsystem.
 It is connected on one side two to "dump" electro-mechanical drives,
 on the other side two a RX11/RXV11/RX211/RXV21 UNIBUS/QBUS interface.
 */
#ifndef _RX0102UCPU_HPP_
#define _RX0102UCPU_HPP_

#include <stdint.h>
#include <string.h>
#include <vector>

#include "rx0102drive.hpp"

class RX11211_c ;
// by default visible to user .. not good
class RX0102uCPU_c: public device_c {
private:

    RX11211_c	*controller ; // driven by that RX11 controller

    // uCPU execute a program sequnece ofthese steps,
    // the current "step" is also the uCPUs "state"
    enum step_e {
        step_none, // if no step being executed
        step_transfer_buffer_write, // controller fills buffer before function execution ()
        step_transfer_buffer_read, // controller reads back buffer (only "empty")
        step_seek, // head movement
        step_head_settle, // if head has moved, it needs time to stabilize
        step_sector_write, // sector buffer to disk surface
        step_sector_read, // disk surface to sector buffer
        step_format_track, // fill all sectors with 00s
        step_seek_next, // step head outwards one track
        step_init_done, // INIT complete
        step_done, // idle between functions
        step_done_read_error_code, // read error register into rxdb
        step_error // done mit error
        /* RX211
        step_DMA_read,
        step_DMA_write,
        step_register_dump,
        */
    } ;


//    timeout_c step_timeout ; // wait period

    /***** program control *****/
    pthread_cond_t on_worker_cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t on_worker_mutex = PTHREAD_MUTEX_INITIALIZER;

    // current program: states executed one-by-one until stop
    // state == busy
    std::vector<enum step_e> program_steps ;
    unsigned program_counter ; // indexes current program_step


    // Are these controller signal lines stabilized on program start or not?
    // unsigned program_selected_drive_unitno ; // 0 or 1
    unsigned program_function_code ; // stabilize against CSR changes
    bool program_function_density ;

    void program_clear(void) ;
    void program_start(void) ;
    bool	program_complete(void) ;

    const char * function_code_text(unsigned function_code) ;
    const char * step_text(enum step_e step) ;

    void step_next(void) ; // advance program counter
    enum step_e step_current() ; // step index by program counter or step_none


    // function switch
    void step_execute(enum step_e step) ;

    void set_powerless(void) ;

    // background worker functions for program steps
    void pgmstep_seek(void) ;


    /***** internal state for various functions *****/

    // see RX docs
    uint16_t rxta ; // track address
    uint16_t rxsa ; // sector address
    uint16_t rxes ;
//    uint16_t rxes ;
    uint16_t complete_rxes(void) ;// error and status
//    uint16_t rxer ;  // extended drive error flags
    void clear_error_codes(void) ;
    void complete_error_codes(void) ;
#ifdef NEEDED
    void set_drive_error(uint16_t error_code) ;
#endif


    // data to read/write on to floppy
    uint8_t	sector_buffer[256] ; // fill empty, read_Sector write-Sector

    // serial command/result exchange
    uint8_t	*transfer_buffer ; // sector buffer or extended status (RX02)
    unsigned transfer_byte_count ; // # of bytes in buffer
    unsigned transfer_byte_idx ; // idx of next byte to read/write

    // after a track-to-track seek, head must settle
    unsigned headsettle_time_ms ;

    bool deleted_data_mark ; // mark of current sector read/written

    // drive to work on
    RX0102drive_c *selected_drive() {
        return drives[signal_selected_drive_unitno] ;
    }

public:
    /***** device_c *****/
    RX0102uCPU_c(RX11211_c *controller) ;
    ~RX0102uCPU_c();
    // virtual, else " undefined reference to `vtable for RX0102uCPU_c'"

    void set_RX02(bool is_RX02) ;

    // http://gunkies.org/wiki/RX01/02_floppy_drive
    // RX01 drive box logic is M7726,M7727
    // RX02 logic is M7744, M7745
    bool is_RX02 ;

    // RX01: [0] is rxer register.
    // RX211: only drive related values valid here
    // are mixed with RX211 related data before DMA
    // [0] word 1 <7:0> definitive error codes, (RX01: RXER)
    // [1] word 1 <15:8> Word Count Register ! SET BY RX211 controller !
    // [2] word 2 <7:0> Current track address of Drive 0
    // [3] word 2 <15:8> Current track address of Drive 1
    // [4] word 3 <7:0> Target Track of Current Disk Access
    // [5] word 3 <15:8> Target Sector of Current Disk Access
    // [6] word 4 <7> Unit Select Bit
    // [6] word 4 <5> Head Load Bit
    // [6] word 4 <6,4> Drive Density Bits of both Drives
    // [6] word 4 <0> Density of Read Error Register Command
    // [7] word 4 <15:8> track address of Selected Drive (only for 0150 error)
    uint8_t  extended_status[8] ;

    bool signal_error_word_count_overflow ;

    // one power switch for the whole box
    parameter_bool_c power_switch = parameter_bool_c(this, "powerswitch", "pwr",/*readonly*/
                                    false, "State of POWER switch");


    void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override; // must implement
    void on_init_changed(void) override; // must implement
    bool on_param_changed(parameter_c *param) override ;

    uint8_t *get_transfer_buffer(uint8_t function_code) ;
    unsigned get_transfer_byte_count(uint8_t function_code, bool double_density) ;
    bool rx2wc_overflow_error(uint8_t function_code, bool double_density, uint16_t rx2wc) ;
    uint16_t  rx2wc() ;

    void worker(unsigned instance) override;


    /***** interface to RX* controller *****/
    // signal lines from RX* controller to uCPU
    unsigned signal_selected_drive_unitno ; // 0 or 1
    unsigned signal_function_code ; // bit<3:1> of CSR
    bool	signal_function_density ; // bit <8> of CSR
    void init() ; // called by on_register_access!
    void go() ; // execute function_code
    // called by on_register_access!

    bool initializing ;

    // signals lines from uCPU to RX* controller, updated with on_uCPU_status_changed()
    bool signal_done ;
    bool signal_error ;
    bool signal_transfer_request ; // next serial word read or writable
    // access to serial data port. function depends on state
    // called by on_register_access!
    void rxdb_after_write(uint16_t w) ;
    void rxdb_after_read(void) ;

    uint16_t rxdb ;  // DATI value of multi function port register


    /***** interface to disk drive *****/
    std::vector<RX0102drive_c *> drives; // the two drive mechanics

    // called asynchronically by disk drive	on image load: "door close", "floppy insert"
    // if it interrupts a program, it like a wild floppy change:
    // do an "illegal sector header error" or the like.
    void on_drive_state_changed(RX0102drive_c *drive) ;



} ;

#endif
