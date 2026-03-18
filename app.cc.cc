/* ====================================================================
   CMPT 361/464 – Assignment 2: Distributed Data Storage System
   PicOS / CC1350
   ==================================================================== */

#include "sysio.h"
#include "ser.h"
#include "serf.h"
#include "tcv.h"
#include "phys_cc1350.h"
#include "plug_null.h"

// ====================================================================
// Constants
// ====================================================================

#define MAX_PACKET_LENGTH   250
#define MAX_DB_ENTRIES      40
#define MAX_RECORD_LEN      20
#define MAX_NEIGHBORS       25
#define WAIT_TIMEOUT_MS     3072    // ~3 seconds in PicOS ticks (1024/sec)

// Message types
#define MSG_DISCOVERY_REQ   0
#define MSG_DISCOVERY_RESP  1
#define MSG_CREATE_RECORD   2
#define MSG_DELETE_RECORD   3
#define MSG_RETRIEVE_RECORD 4
#define MSG_RESPONSE        5

// Response status codes
#define STATUS_OK           0x01
#define STATUS_DB_FULL      0x02
#define STATUS_DELETE_FAIL  0x03
#define STATUS_RETRIEVE_FAIL 0x04

// Packet sizes (netID[2] + payload + CRC[2])
// Discovery req/resp:  netID(2) + grpID(2)+type(1)+reqnum(1)+snd(1)+rcv(1) + CRC(2) = 10
#define PKT_DISCOVERY_SZ    10
// Create:  netID(2) + grpID(2)+type(1)+reqnum(1)+snd(1)+rcv(1)+record(20) + CRC(2) = 30
#define PKT_CREATE_SZ       30
// Delete/Retrieve: netID(2) + grpID(2)+type(1)+reqnum(1)+snd(1)+rcv(1)+idx(1)+pad(1) + CRC(2) = 12
#define PKT_DELRETR_SZ      12
// Response: netID(2) + grpID(2)+type(1)+reqnum(1)+snd(1)+rcv(1)+status(1)+pad(1)+record(20) + CRC(2) = 32
#define PKT_RESPONSE_SZ     32

// ====================================================================
// Data structures
// ====================================================================

struct db_entry {
    byte  owner_id;
    word  timestamp;
    char  record[MAX_RECORD_LEN];
    byte  occupied;
};

// ====================================================================
// Global state
// ====================================================================

int  sfd = -1;

word node_group_id  = 1;
byte node_id        = 1;

struct db_entry database[MAX_DB_ENTRIES];

byte neighbor_list[MAX_NEIGHBORS];
int  neighbor_count = 0;

// Pending response tracking
byte pending_req_num    = 0;
byte pending_resp_type  = 0xFF;   // 0xFF = nothing pending
byte response_received  = 0;
byte response_status    = 0;
char response_record[MAX_RECORD_LEN];

// ====================================================================
// Helpers
// ====================================================================

int db_count() {
    int i, n = 0;
    for (i = 0; i < MAX_DB_ENTRIES; i++)
        if (database[i].occupied) n++;
    return n;
}

int db_free_slot() {
    int i;
    for (i = 0; i < MAX_DB_ENTRIES; i++)
        if (!database[i].occupied) return i;
    return -1;
}

// Simple LFSR pseudo-random byte
byte rand_byte() {
    static word seed = 0xACE1;
    seed ^= (seed << 7);
    seed ^= (seed >> 9);
    seed ^= (seed << 8);
    return (byte)(seed & 0xFF);
}

// ====================================================================
// Send helpers
// (All follow the tutorial pattern: pkt[0]=0, then cast pkt+1 to
//  a byte pointer and fill the payload fields)
// ====================================================================

void send_discovery_request() {
    address pkt;
    byte req_num = rand_byte();

    pkt = tcv_wnp(WNONE, sfd, PKT_DISCOVERY_SZ);
    if (pkt == NULL) return;

    pkt[0] = 0;
    byte *p = (byte*)(pkt + 1);
    *p++ = (byte)(node_group_id >> 8);
    *p++ = (byte)(node_group_id & 0xFF);
    *p++ = MSG_DISCOVERY_REQ;
    *p++ = req_num;
    *p++ = node_id;
    *p++ = 0;   // receiver = broadcast

    tcv_endp(pkt);
}

void send_discovery_response(byte req_num, byte dest_id) {
    address pkt;

    pkt = tcv_wnp(WNONE, sfd, PKT_DISCOVERY_SZ);
    if (pkt == NULL) return;

    pkt[0] = 0;
    byte *p = (byte*)(pkt + 1);
    *p++ = (byte)(node_group_id >> 8);
    *p++ = (byte)(node_group_id & 0xFF);
    *p++ = MSG_DISCOVERY_RESP;
    *p++ = req_num;
    *p++ = node_id;
    *p++ = dest_id;

    tcv_endp(pkt);
}

void send_create_record(byte dest_id, const char *rec) {
    address pkt;
    byte req_num = rand_byte();
    int i;

    pending_req_num   = req_num;
    pending_resp_type = MSG_CREATE_RECORD;
    response_received = 0;

    pkt = tcv_wnp(WNONE, sfd, PKT_CREATE_SZ);
    if (pkt == NULL) return;

    pkt[0] = 0;
    byte *p = (byte*)(pkt + 1);
    *p++ = (byte)(node_group_id >> 8);
    *p++ = (byte)(node_group_id & 0xFF);
    *p++ = MSG_CREATE_RECORD;
    *p++ = req_num;
    *p++ = node_id;
    *p++ = dest_id;
    for (i = 0; i < MAX_RECORD_LEN - 1; i++)
        *p++ = rec[i] ? (byte)rec[i] : 0;
    *p = 0; // ensure null terminator

    tcv_endp(pkt);
}

void send_delete_record(byte dest_id, byte rec_index) {
    address pkt;
    byte req_num = rand_byte();

    pending_req_num   = req_num;
    pending_resp_type = MSG_DELETE_RECORD;
    response_received = 0;

    pkt = tcv_wnp(WNONE, sfd, PKT_DELRETR_SZ);
    if (pkt == NULL) return;

    pkt[0] = 0;
    byte *p = (byte*)(pkt + 1);
    *p++ = (byte)(node_group_id >> 8);
    *p++ = (byte)(node_group_id & 0xFF);
    *p++ = MSG_DELETE_RECORD;
    *p++ = req_num;
    *p++ = node_id;
    *p++ = dest_id;
    *p++ = rec_index;
    *p++ = 0;   // padding

    tcv_endp(pkt);
}

void send_retrieve_record(byte dest_id, byte rec_index) {
    address pkt;
    byte req_num = rand_byte();

    pending_req_num   = req_num;
    pending_resp_type = MSG_RETRIEVE_RECORD;
    response_received = 0;

    pkt = tcv_wnp(WNONE, sfd, PKT_DELRETR_SZ);
    if (pkt == NULL) return;

    pkt[0] = 0;
    byte *p = (byte*)(pkt + 1);
    *p++ = (byte)(node_group_id >> 8);
    *p++ = (byte)(node_group_id & 0xFF);
    *p++ = MSG_RETRIEVE_RECORD;
    *p++ = req_num;
    *p++ = node_id;
    *p++ = dest_id;
    *p++ = rec_index;
    *p++ = 0;   // padding

    tcv_endp(pkt);
}

void send_response(byte req_num, byte dest_id, byte status, const char *rec) {
    address pkt;
    int i;

    pkt = tcv_wnp(WNONE, sfd, PKT_RESPONSE_SZ);
    if (pkt == NULL) return;

    pkt[0] = 0;
    byte *p = (byte*)(pkt + 1);
    *p++ = (byte)(node_group_id >> 8);
    *p++ = (byte)(node_group_id & 0xFF);
    *p++ = MSG_RESPONSE;
    *p++ = req_num;
    *p++ = node_id;
    *p++ = dest_id;
    *p++ = status;
    *p++ = 0;   // padding
    for (i = 0; i < MAX_RECORD_LEN; i++)
        *p++ = (rec && rec[i]) ? (byte)rec[i] : 0;

    tcv_endp(pkt);
}

// ====================================================================
// Incoming packet handler (called from receiver FSM)
// ====================================================================

void handle_incoming(address pkt) {
    byte *p = (byte*)(pkt + 1);

    word  grp        = ((word)p[0] << 8) | p[1];
    byte  msg_type   = p[2];
    byte  req_num    = p[3];
    byte  sender_id  = p[4];
    byte  recv_id    = p[5];

    switch (msg_type) {

        case MSG_DISCOVERY_REQ:
            if (grp == node_group_id)
                send_discovery_response(req_num, sender_id);
            break;

        case MSG_DISCOVERY_RESP:
            if (recv_id == node_id && grp == node_group_id) {
                int i;
                for (i = 0; i < neighbor_count; i++)
                    if (neighbor_list[i] == sender_id) return;
                if (neighbor_count < MAX_NEIGHBORS)
                    neighbor_list[neighbor_count++] = sender_id;
            }
            break;

        case MSG_CREATE_RECORD:
            if (recv_id != node_id || grp != node_group_id) return;
            {
                int slot = db_free_slot();
                if (slot < 0) {
                    send_response(req_num, sender_id, STATUS_DB_FULL, NULL);
                } else {
                    int i;
                    database[slot].owner_id  = sender_id;
                    database[slot].timestamp = (word)seconds();
                    database[slot].occupied  = 1;
                    for (i = 0; i < MAX_RECORD_LEN; i++)
                        database[slot].record[i] = (char)p[6 + i];
                    database[slot].record[MAX_RECORD_LEN - 1] = '\0';
                    send_response(req_num, sender_id, STATUS_OK, NULL);
                }
            }
            break;

        case MSG_DELETE_RECORD:
            if (recv_id != node_id || grp != node_group_id) return;
            {
                byte idx = p[6];
                if (idx >= MAX_DB_ENTRIES || !database[idx].occupied) {
                    send_response(req_num, sender_id, STATUS_DELETE_FAIL, NULL);
                } else {
                    database[idx].occupied  = 0;
                    database[idx].record[0] = '\0';
                    send_response(req_num, sender_id, STATUS_OK, NULL);
                }
            }
            break;

        case MSG_RETRIEVE_RECORD:
            if (recv_id != node_id || grp != node_group_id) return;
            {
                byte idx = p[6];
                if (idx >= MAX_DB_ENTRIES || !database[idx].occupied) {
                    send_response(req_num, sender_id, STATUS_RETRIEVE_FAIL, NULL);
                } else {
                    send_response(req_num, sender_id, STATUS_OK,
                                  database[idx].record);
                }
            }
            break;

        case MSG_RESPONSE:
            if (recv_id != node_id || grp != node_group_id) return;
            if (req_num == pending_req_num && pending_resp_type != 0xFF) {
                int i;
                response_status = p[6];
                for (i = 0; i < MAX_RECORD_LEN; i++)
                    response_record[i] = (char)p[8 + i];
                response_record[MAX_RECORD_LEN - 1] = '\0';
                response_received  = 1;
                pending_resp_type  = 0xFF;
            }
            break;

        default:
            break;
    }
}

// ====================================================================
// RECEIVER FSM
// (matches the PowerRSSI runfsm receiver pattern exactly)
// ====================================================================

fsm receiver {
    address rpkt;

    state RC_WAIT:
        rpkt = tcv_rnp(RC_WAIT, sfd);
        handle_incoming(rpkt);
        tcv_endp(rpkt);
        proceed RC_WAIT;
}

// ====================================================================
// MAIN (root) FSM
// ====================================================================

fsm root {

    int   temp;
    byte  dest_id;
    byte  rec_index;
    char  rec_buf[MAX_RECORD_LEN];
    int   i;

    // ------------------------------------------------------------------
    state INIT:
        // Init radio
        phys_cc1350(0, MAX_PACKET_LENGTH);
        tcv_plug(0, &plug_null);
        sfd = tcv_open(WNONE, 0, 0);

        if (sfd < 0) {
            diag("Cannot open tcv interface");
            halt();
        }

        tcv_control(sfd, PHYSOPT_ON, NULL);

        // Clear database
        for (i = 0; i < MAX_DB_ENTRIES; i++) {
            database[i].occupied  = 0;
            database[i].record[0] = '\0';
        }

        runfsm receiver;
        proceed MENU;

    // ------------------------------------------------------------------
    state MENU:
        ser_outf(MENU,
            "\r\nGroup %u Device #%u (%d/%d records)\r\n"
            "(G)roup ID\r\n"
            "(N)ew device ID\r\n"
            "(F)ind neighbors\r\n"
            "(C)reate record on neighbor\r\n"
            "(D)elete record on neighbor\r\n"
            "(R)etrieve record from neighbor\r\n"
            "(S)how local records\r\n"
            "R(e)set local storage\r\n\r\n"
            "Selection: ",
            node_group_id,
            (word)node_id,
            db_count(),
            MAX_DB_ENTRIES
        );

    state GET_SEL:
        ser_inf(GET_SEL, "%c", rec_buf);  // reuse rec_buf for single char
        switch (rec_buf[0]) {
            case 'G': case 'g': proceed CMD_GROUP;
            case 'N': case 'n': proceed CMD_NODE;
            case 'F': case 'f': proceed CMD_FIND;
            case 'C': case 'c': proceed CMD_CREATE;
            case 'D': case 'd': proceed CMD_DELETE;
            case 'R': case 'r': proceed CMD_RETRIEVE;
            case 'S': case 's': proceed CMD_SHOW;
            case 'E': case 'e': proceed CMD_RESET;
            default:
                ser_out(GET_SEL, "\r\nInvalid selection.\r\n");
                proceed MENU;
        }

    // ------------------------------------------------------------------
    // G – Change Group ID
    // ------------------------------------------------------------------
    state CMD_GROUP:
        ser_out(CMD_GROUP, "\r\nEnter new Group ID: ");

    state CMD_GROUP_READ:
        ser_inf(CMD_GROUP_READ, "%d", &temp);
        if (temp <= 0) {
            ser_out(CMD_GROUP_READ, "\r\nInvalid Group ID.\r\n");
            proceed MENU;
        }
        node_group_id = (word)temp;
        ser_outf(CMD_GROUP_READ, "\r\nGroup ID set to %u\r\n", node_group_id);
        proceed MENU;

    // ------------------------------------------------------------------
    // N – Change Node ID
    // ------------------------------------------------------------------
    state CMD_NODE:
        ser_out(CMD_NODE, "\r\nEnter new Node ID (1-25): ");

    state CMD_NODE_READ:
        ser_inf(CMD_NODE_READ, "%d", &temp);
        if (temp < 1 || temp > 25) {
            ser_out(CMD_NODE_READ, "\r\nInvalid Node ID. Must be 1-25.\r\n");
            proceed MENU;
        }
        node_id = (byte)temp;
        ser_outf(CMD_NODE_READ, "\r\nNode ID set to %u\r\n", (word)node_id);
        proceed MENU;

    // ------------------------------------------------------------------
    // F – Find Neighbors (broadcast twice, 3-sec wait each time)
    // ------------------------------------------------------------------
    state CMD_FIND:
        neighbor_count = 0;
        send_discovery_request();
        delay(WAIT_TIMEOUT_MS, CMD_FIND2);
        release;

    state CMD_FIND2:
        send_discovery_request();
        delay(WAIT_TIMEOUT_MS, CMD_FIND3);
        release;

    state CMD_FIND3:
        ser_out(CMD_FIND3, "\r\n Neighbors: ");

    state CMD_FIND_PRINT:
        if (neighbor_count == 0) {
            ser_out(CMD_FIND_PRINT, "None\r\n");
        } else {
            for (i = 0; i < neighbor_count; i++)
                ser_outf(CMD_FIND_PRINT, "%u ", (word)neighbor_list[i]);
            ser_out(CMD_FIND_PRINT, "\r\n");
        }
        proceed MENU;

    // ------------------------------------------------------------------
    // C – Create record on neighbor
    // ------------------------------------------------------------------
    state CMD_CREATE:
        ser_out(CMD_CREATE, "\r\nEnter destination Node ID: ");

    state CMD_CREATE_DEST:
        ser_inf(CMD_CREATE_DEST, "%d", &temp);
        if (temp < 1 || temp > 25) {
            ser_out(CMD_CREATE_DEST, "\r\nInvalid Node ID.\r\n");
            proceed MENU;
        }
        dest_id = (byte)temp;
        ser_out(CMD_CREATE_DEST, "\r\nEnter record string (max 19 chars): ");

    state CMD_CREATE_REC:
        ser_inf(CMD_CREATE_REC, "%s", rec_buf);
        rec_buf[MAX_RECORD_LEN - 1] = '\0';
        send_create_record(dest_id, rec_buf);
        delay(WAIT_TIMEOUT_MS, CMD_CREATE_TIMEOUT);
        release;

    state CMD_CREATE_WAIT:
        if (!response_received) {
            delay(100, CMD_CREATE_WAIT);
            release;
        }
        proceed CMD_CREATE_DONE;

    state CMD_CREATE_TIMEOUT:
        if (response_received) proceed CMD_CREATE_DONE;
        pending_resp_type = 0xFF;
        ser_out(CMD_CREATE_TIMEOUT, "\r\nFailed to reach the destination\r\n");
        proceed MENU;

    state CMD_CREATE_DONE:
        if (response_status == STATUS_OK)
            ser_out(CMD_CREATE_DONE, "\r\n Data Saved\r\n");
        else
            ser_outf(CMD_CREATE_DONE,
                "\r\n The record can't be saved on node %u\r\n",
                (word)dest_id);
        proceed MENU;

    // ------------------------------------------------------------------
    // D – Delete record on neighbor
    // ------------------------------------------------------------------
    state CMD_DELETE:
        ser_out(CMD_DELETE, "\r\nEnter destination Node ID: ");

    state CMD_DELETE_DEST:
        ser_inf(CMD_DELETE_DEST, "%d", &temp);
        if (temp < 1 || temp > 25) {
            ser_out(CMD_DELETE_DEST, "\r\nInvalid Node ID.\r\n");
            proceed MENU;
        }
        dest_id = (byte)temp;
        ser_out(CMD_DELETE_DEST, "\r\nEnter record index to delete (0-39): ");

    state CMD_DELETE_IDX:
        ser_inf(CMD_DELETE_IDX, "%d", &temp);
        if (temp < 0 || temp >= MAX_DB_ENTRIES) {
            ser_out(CMD_DELETE_IDX, "\r\nInvalid index.\r\n");
            proceed MENU;
        }
        rec_index = (byte)temp;
        send_delete_record(dest_id, rec_index);
        delay(WAIT_TIMEOUT_MS, CMD_DELETE_TIMEOUT);
        release;

    state CMD_DELETE_TIMEOUT:
        if (response_received) proceed CMD_DELETE_DONE;
        pending_resp_type = 0xFF;
        ser_out(CMD_DELETE_TIMEOUT, "\r\nFailed to reach the destination\r\n");
        proceed MENU;

    state CMD_DELETE_DONE:
        if (response_status == STATUS_OK)
            ser_out(CMD_DELETE_DONE, "\r\n Record Deleted\r\n");
        else
            ser_outf(CMD_DELETE_DONE,
                "\r\n The record doesnot exists on node %u\r\n",
                (word)dest_id);
        proceed MENU;

    // ------------------------------------------------------------------
    // R – Retrieve record from neighbor
    // ------------------------------------------------------------------
    state CMD_RETRIEVE:
        ser_out(CMD_RETRIEVE, "\r\nEnter destination Node ID: ");

    state CMD_RETRIEVE_DEST:
        ser_inf(CMD_RETRIEVE_DEST, "%d", &temp);
        if (temp < 1 || temp > 25) {
            ser_out(CMD_RETRIEVE_DEST, "\r\nInvalid Node ID.\r\n");
            proceed MENU;
        }
        dest_id = (byte)temp;
        ser_out(CMD_RETRIEVE_DEST, "\r\nEnter record index to retrieve (0-39): ");

    state CMD_RETRIEVE_IDX:
        ser_inf(CMD_RETRIEVE_IDX, "%d", &temp);
        if (temp < 0 || temp >= MAX_DB_ENTRIES) {
            ser_out(CMD_RETRIEVE_IDX, "\r\nInvalid index.\r\n");
            proceed MENU;
        }
        rec_index = (byte)temp;
        send_retrieve_record(dest_id, rec_index);
        delay(WAIT_TIMEOUT_MS, CMD_RETRIEVE_TIMEOUT);
        release;

    state CMD_RETRIEVE_TIMEOUT:
        if (response_received) proceed CMD_RETRIEVE_DONE;
        pending_resp_type = 0xFF;
        ser_out(CMD_RETRIEVE_TIMEOUT, "\r\nFailed to reach the destination\r\n");
        proceed MENU;

    state CMD_RETRIEVE_DONE:
        if (response_status == STATUS_OK)
            ser_outf(CMD_RETRIEVE_DONE,
                "\r\n Record Received from %u: %s\r\n",
                (word)dest_id, response_record);
        else
            ser_outf(CMD_RETRIEVE_DONE,
                "\r\n The record does not exist on node %u\r\n",
                (word)dest_id);
        proceed MENU;

    // ------------------------------------------------------------------
    // S – Show local records
    // ------------------------------------------------------------------
    state CMD_SHOW:
        ser_out(CMD_SHOW,
            "\r\nIndex\tTime Stamp\tOwner ID\tRecord Data\r\n");

    state CMD_SHOW_LOOP:
        for (i = 0; i < MAX_DB_ENTRIES; i++) {
            if (database[i].occupied) {
                ser_outf(CMD_SHOW_LOOP, "%d\t%u\t\t%u\t\t%s\r\n",
                    i,
                    database[i].timestamp,
                    (word)database[i].owner_id,
                    database[i].record);
            }
        }
        proceed MENU;

    // ------------------------------------------------------------------
    // E – Reset local storage
    // ------------------------------------------------------------------
    state CMD_RESET:
        for (i = 0; i < MAX_DB_ENTRIES; i++) {
            database[i].occupied  = 0;
            database[i].record[0] = '\0';
        }
        ser_out(CMD_RESET, "\r\nLocal storage cleared.\r\n");
        proceed MENU;
}
