/**
 * @file rpc_connect_handlers.cc
 * @brief Handlers for session management discconnect requests and responses.
 */
#include "rpc.h"
#include <algorithm>

namespace ERpc {

// We don't need to check remote arguments since the session was already
// connected successfully.
//
// We don't need to lock the session since it is idle, i.e., the session client
// has received responses for all outstanding requests.

template <class TTr>
void Rpc<TTr>::handle_disconnect_req_st(typename Nexus<TTr>::SmWorkItem *wi) {
  assert(in_creator());
  assert(wi != nullptr && wi->epeer != nullptr);

  const SessionMgmtPkt *sm_pkt = wi->sm_pkt;
  assert(sm_pkt != nullptr &&
         sm_pkt->pkt_type == SessionMgmtPktType::kDisconnectReq);

  // Check that the server fields known by the client were filled correctly
  assert(sm_pkt->server.rpc_id == rpc_id);
  assert(strcmp(sm_pkt->server.hostname, nexus->hostname.c_str()) == 0);

  // Create the basic issue message
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg, "eRPC Rpc %u: Received disconnect request from %s. Issue",
          rpc_id, sm_pkt->client.name().c_str());

  // Do some sanity checks
  uint16_t session_num = sm_pkt->server.session_num;
  assert(session_num < session_vec.size());

  Session *session = session_vec.at(session_num);  // The server end point
  assert(session != nullptr);
  assert(session->is_server());
  assert(session->server == sm_pkt->server);
  assert(session->client == sm_pkt->client);

  // Check that responses for all sslots have been sent
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    SSlot &sslot = session->sslot_arr[i];
    assert(sslot.rx_msgbuf.buf == nullptr &&
           sslot.rx_msgbuf.buffer.buf == nullptr);

    if (sslot.tx_msgbuf != nullptr) {
      assert(sslot.tx_msgbuf->pkts_queued == sslot.tx_msgbuf->num_pkts);
    }
  }

  erpc_dprintf("%s. None. Sending response.\n", issue_msg);
  enqueue_sm_resp(wi, SessionMgmtErrType::kNoError);

  bury_session_st(session);  // Free session resources + NULL in session_vec
}

// We don't need to acquire the session lock because this session has been
// idle since the disconnect request was sent.
template <class TTr>
void Rpc<TTr>::handle_disconnect_resp_st(SessionMgmtPkt *sm_pkt) {
  assert(in_creator());
  assert(sm_pkt != nullptr);
  assert(sm_pkt->pkt_type == SessionMgmtPktType::kDisconnectResp);
  assert(session_mgmt_err_type_is_valid(sm_pkt->err_type));

  // Create the basic issue message using only the packet
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %u: Received disconnect response from %s for session %u. "
          "Issue",
          rpc_id, sm_pkt->server.name().c_str(), sm_pkt->client.session_num);

  // Try to locate the requester session and do some sanity checks
  uint16_t session_num = sm_pkt->client.session_num;
  assert(session_num < session_vec.size());

  Session *session = session_vec[session_num];
  assert(session != nullptr);
  assert(session->is_client());
  assert(session->state == SessionState::kDisconnectInProgress);
  assert(session->client_info.sm_api_req_pending);
  assert(session->client == sm_pkt->client);
  assert(session->server == sm_pkt->server);
  // Disconnect requests can only succeed
  assert(sm_pkt->err_type == SessionMgmtErrType::kNoError);

  session->client_info.sm_api_req_pending = false;

  session->state = SessionState::kDisconnected;  // Mark session connected

  if (!session->client_info.sm_callbacks_disabled) {
    erpc_dprintf("%s: None. Session disconnected.\n", issue_msg);
    session_mgmt_handler(session->local_session_num,
                         SessionMgmtEventType::kDisconnected,
                         SessionMgmtErrType::kNoError, context);
  } else {
    erpc_dprintf(
        "%s: None. Session disconnected. Not invoking disconnect "
        "callback because session was never connected successfully.",
        issue_msg);
  }

  bury_session_st(session);  // Free session resources + NULL in session_vec
}

}  // End ERpc
