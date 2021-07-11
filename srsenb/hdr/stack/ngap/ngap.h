/**
 * Copyright 2013-2021 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */
#ifndef SRSENB_NGAP_H
#define SRSENB_NGAP_H

#include "srsenb/hdr/common/common_enb.h"
#include "srsran/adt/circular_map.h"
#include "srsran/adt/optional.h"
#include "srsran/asn1/asn1_utils.h"
#include "srsran/asn1/ngap.h"
#include "srsran/common/bcd_helpers.h"
#include "srsran/common/buffer_pool.h"
#include "srsran/common/common.h"
#include "srsran/common/network_utils.h"
#include "srsran/common/stack_procedure.h"
#include "srsran/common/standard_streams.h"
#include "srsran/common/task_scheduler.h"
#include "srsran/common/threads.h"
#include "srsran/interfaces/gnb_ngap_interfaces.h"
#include "srsran/interfaces/gnb_rrc_nr_interfaces.h"
#include "srsran/srslog/srslog.h"
#include <iostream>
#include <unordered_map>

namespace srsenb {

class ngap : public ngap_interface_rrc_nr
{
public:
  ngap(srsran::task_sched_handle   task_sched_,
       srslog::basic_logger&       logger,
       srsran::socket_manager_itf* rx_socket_handler);
  int  init(const ngap_args_t& args_, rrc_interface_ngap_nr* rrc_);
  void stop();

  // RRC NR interface
  void initial_ue(uint16_t                                rnti,
                  uint32_t                                gnb_cc_idx,
                  asn1::ngap_nr::rrcestablishment_cause_e cause,
                  srsran::unique_byte_buffer_t            pdu);
  void initial_ue(uint16_t                                rnti,
                  uint32_t                                gnb_cc_idx,
                  asn1::ngap_nr::rrcestablishment_cause_e cause,
                  srsran::unique_byte_buffer_t            pdu,
                  uint32_t                                s_tmsi);

  void write_pdu(uint16_t rnti, srsran::unique_byte_buffer_t pdu){};
  bool user_exists(uint16_t rnti) { return true; };
  void user_mod(uint16_t old_rnti, uint16_t new_rnti){};
  bool user_release(uint16_t rnti, asn1::ngap_nr::cause_radio_network_e cause_radio) { return true; };
  bool is_amf_connected();
  bool send_error_indication(const asn1::ngap_nr::cause_c& cause,
                             srsran::optional<uint32_t>    ran_ue_ngap_id = {},
                             srsran::optional<uint32_t>    amf_ue_ngap_id = {});
  void ue_ctxt_setup_complete(uint16_t rnti);

  // Stack interface
  bool
  handle_amf_rx_msg(srsran::unique_byte_buffer_t pdu, const sockaddr_in& from, const sctp_sndrcvinfo& sri, int flags);

private:
  static const int AMF_PORT        = 38412;
  static const int ADDR_FAMILY     = AF_INET;
  static const int SOCK_TYPE       = SOCK_STREAM;
  static const int PROTO           = IPPROTO_SCTP;
  static const int PPID            = 60;
  static const int NONUE_STREAM_ID = 0;

  // args
  rrc_interface_ngap_nr*      rrc = nullptr;
  ngap_args_t                 args = {};
  srslog::basic_logger&       logger;
  srsran::task_sched_handle   task_sched;
  srsran::task_queue_handle   amf_task_queue;
  srsran::socket_manager_itf* rx_socket_handler;

  srsran::unique_socket amf_socket;
  struct sockaddr_in    amf_addr            = {}; // AMF address
  bool                  amf_connected       = false;
  bool                  running             = false;
  uint32_t              next_gnb_ue_ngap_id = 1; // Next GNB-side UE identifier
  uint16_t              next_ue_stream_id   = 1; // Next UE SCTP stream identifier
  srsran::unique_timer  amf_connect_timer, ngsetup_timeout;

  // Protocol IEs sent with every UL NGAP message
  asn1::ngap_nr::tai_s    tai;
  asn1::ngap_nr::nr_cgi_s nr_cgi;

  // Moved into NGAP class to avoid redifinition (Introduce new namespace?)
  struct ue_ctxt_t {
    static const uint32_t invalid_gnb_id = std::numeric_limits<uint32_t>::max();

    uint16_t                   rnti           = SRSRAN_INVALID_RNTI;
    uint32_t                   ran_ue_ngap_id = invalid_gnb_id;
    srsran::optional<uint32_t> amf_ue_ngap_id;
    uint32_t                   gnb_cc_idx     = 0;
    struct timeval             init_timestamp = {};

    // AMF identifier
    uint16_t amf_set_id;
    uint8_t  amf_pointer;
    uint8_t  amf_region_id;
  };

  asn1::ngap_nr::ng_setup_resp_s ngsetupresponse;

  int  build_tai_cgi();
  bool connect_amf();
  bool setup_ng();
  bool sctp_send_ngap_pdu(const asn1::ngap_nr::ngap_pdu_c& tx_pdu, uint32_t rnti, const char* procedure_name);

  bool handle_ngap_rx_pdu(srsran::byte_buffer_t* pdu);
  bool handle_successfuloutcome(const asn1::ngap_nr::successful_outcome_s& msg);
  bool handle_unsuccessfuloutcome(const asn1::ngap_nr::unsuccessful_outcome_s& msg);
  bool handle_initiatingmessage(const asn1::ngap_nr::init_msg_s& msg);

  bool handle_dlnastransport(const asn1::ngap_nr::dl_nas_transport_s& msg);
  bool handle_ngsetupresponse(const asn1::ngap_nr::ng_setup_resp_s& msg);
  bool handle_ngsetupfailure(const asn1::ngap_nr::ng_setup_fail_s& msg);
  bool handle_initialctxtsetuprequest(const asn1::ngap_nr::init_context_setup_request_s& msg);
  struct ue {
    explicit ue(ngap* ngap_ptr_);
    bool send_initialuemessage(asn1::ngap_nr::rrcestablishment_cause_e cause,
                               srsran::unique_byte_buffer_t            pdu,
                               bool                                    has_tmsi,
                               uint32_t                                s_tmsi = 0);
    bool send_ulnastransport(srsran::unique_byte_buffer_t pdu);
    bool was_uectxtrelease_requested() const { return release_requested; }
    void ue_ctxt_setup_complete();

    ue_ctxt_t ctxt      = {};
    uint16_t  stream_id = 1;

  private:
    // args
    ngap* ngap_ptr;

    // state
    bool release_requested = false;
  };
  class user_list
  {
  public:
    using value_type     = std::unique_ptr<ue>;
    using iterator       = std::unordered_map<uint32_t, value_type>::iterator;
    using const_iterator = std::unordered_map<uint32_t, value_type>::const_iterator;
    using pair_type      = std::unordered_map<uint32_t, value_type>::value_type;

    ue*            find_ue_rnti(uint16_t rnti);
    ue*            find_ue_gnbid(uint32_t gnbid);
    ue*            find_ue_amfid(uint32_t amfid);
    ue*            add_user(value_type user);
    void           erase(ue* ue_ptr);
    iterator       begin() { return users.begin(); }
    iterator       end() { return users.end(); }
    const_iterator cbegin() const { return users.begin(); }
    const_iterator cend() const { return users.end(); }
    size_t         size() const { return users.size(); }

  private:
    std::unordered_map<uint32_t, std::unique_ptr<ue> > users; // maps ran_ue_ngap_id to user
  };
  user_list users;

  // procedures
  class ng_setup_proc_t
  {
  public:
    struct ngsetupresult {
      bool success = false;
      enum class cause_t { timeout, failure } cause;
    };

    explicit ng_setup_proc_t(ngap* ngap_) : ngap_ptr(ngap_) {}
    srsran::proc_outcome_t init();
    srsran::proc_outcome_t step() { return srsran::proc_outcome_t::yield; }
    srsran::proc_outcome_t react(const ngsetupresult& event);
    void                   then(const srsran::proc_state_t& result) const;
    const char*            name() const { return "AMF Connection"; }

  private:
    srsran::proc_outcome_t start_amf_connection();

    ngap* ngap_ptr = nullptr;
  };

  ue* handle_ngapmsg_ue_id(uint32_t gnb_id, uint32_t amf_id);

  srsran::proc_t<ng_setup_proc_t> ngsetup_proc;

  std::string get_cause(const asn1::ngap_nr::cause_c& c);
  void        log_ngap_msg(const asn1::ngap_nr::ngap_pdu_c& msg, srsran::const_span<uint8_t> sdu, bool is_rx);
};

} // namespace srsenb
#endif