// The contents of this file are subject to the Mozilla Public License
// Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// http://www.mozilla.org/MPL/ 
// 
// Software distributed under the License is distributed on an "AS IS"
// basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
// License for the specific language governing rights and limitations
// under the License. 
// 
// The Original Code is the Berkeley Open Infrastructure for Network Computing. 
// 
// The Initial Developer of the Original Code is the SETI@home project.
// Portions created by the SETI@home project are Copyright (C) 2002
// University of California at Berkeley. All Rights Reserved. 
// 
// Contributor(s):
//

#define DEBUG

#ifndef _SCHEDULER_OP_
#define _SCHEDULER_OP_

// SCHEDULER_OP encapsulates the policy and mechanism
// for communicating with scheduling servers.
// It is implemented as a finite-state machine.
// It is active in one of two modes:
//    get_work: the client wants to get work, and possibly to
//       return results as a side-effect
//    return_results: the client wants to return results, and possibly
//       to get work as a side-effect
// 

#include "client_types.h"
#include "http.h"
#include "prefs.h"

// constants related to scheduler RPC policy

#define MASTER_FETCH_PERIOD     10
    // fetch and parse master URL if nrpc_failures is a multiple of this
#define RETRY_BASE_PERIOD       1
    // after failure, back off 2^nrpc_failures times this times random
#define RETRY_CAP               10
    // cap on nrpc_failures in the above formula
#define MASTER_FETCH_RETRY_CAP 3
    // cap on how many times we will contact master_url
    // before moving into a state in which we will not
    // exponentially backoff anymore but rather contact the master URL
    // at the frequency below
#define MASTER_FETCH_INTERVAL (60*60*24*7*2)    // 2 weeks 
    // This is the Max on the time to wait after we've contacted the Master URL MASTER_FETCH_RETRY_CAP times.

//The next two constants are used to bound RPC exponential waiting. 
#define SCHED_RETRY_DELAY_MIN    60                // 1 minute
#define SCHED_RETRY_DELAY_MAX    (60*60*4)         // 4 hours

#ifdef DEBUG
#define MASTER_FETCH_PERIOD     5
#define RETRY_BASE_PERIOD       1
#define RETRY_CAP               5
#define MASTER_FETCH_RETRY_CAP 3
#define MASTER_FETCH_INTERVAL 5
#define SCHED_RETRY_DELAY_MIN    1
#define SCHED_RETRY_DELAY_MAX    30
#endif

#define SCHEDULER_OP_STATE_IDLE         0
#define SCHEDULER_OP_STATE_GET_MASTER   1
#define SCHEDULER_OP_STATE_RPC          2

struct SCHEDULER_OP {
    int state;
    int scheduler_op_retval;
    HTTP_OP http_op;
    HTTP_OP_SET* http_ops;
    PROJECT* project;               // project we're currently contacting
    char scheduler_url[256];
    bool must_get_work;             // true iff in get_work mode
    unsigned int url_index;         // index within project's URL list

    SCHEDULER_OP(HTTP_OP_SET*);
    bool poll();
    int init_get_work();
    int init_return_results(PROJECT*, double nsecs);
    int init_op_project(double ns);
    int init_master_fetch(PROJECT*);
    int set_min_rpc_time(PROJECT*);
    bool update_urls(PROJECT& project, vector<STRING256> &urls);
    int start_op(PROJECT*);
    int backoff(PROJECT* p, char *error_msg);
    int start_rpc();
    int parse_master_file(vector<STRING256>&);
};

struct SCHEDULER_REPLY {
    int hostid;
    double host_total_credit;
    double host_expavg_credit;
    unsigned int host_create_time;
    int request_delay;
    char message[1024];
    char message_priority[256];
    char project_name[256];
    char* global_prefs_xml;     // not including <global_preferences> tags
    char* project_prefs_xml;    // not including <project_preferences> tags
    char user_name[256];
    double user_total_credit;
    double user_expavg_credit;
    unsigned int user_create_time;
    vector<APP> apps;
    vector<FILE_INFO> file_infos;
    vector<APP_VERSION> app_versions;
    vector<WORKUNIT> workunits;
    vector<RESULT> results;
    vector<RESULT> result_acks;
    char* code_sign_key;
    char* code_sign_key_signature;

    SCHEDULER_REPLY();
    ~SCHEDULER_REPLY();
    int parse(FILE*);
};

#endif
