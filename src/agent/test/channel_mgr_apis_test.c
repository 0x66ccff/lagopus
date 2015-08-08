/*
 * Copyright 2014-2015 Nippon Telegraph and Telephone Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "unity.h"
#include "lagopus_apis.h"
#include "lagopus/port.h"
#include "lagopus/flowdb.h"
#include "lagopus/dp_apis.h"
#include "event.h"
#include "../channel.h"
#include "../channel_mgr.h"


int s4 = -1;
static struct event_manager *em;
static datastore_interface_info_t interface_info;
static datastore_bridge_info_t bridge_info;
static bool run;
static pthread_t p;
static pthread_mutex_t cond_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static uint64_t dpid = 0xabc;
static const char *bridge_name = "test_bridge01";
static const char *port_name = "test_port01";
static const char *interface_name = "test_if01";

void *
loop(void *arg) {
  struct event *event;

  (void) arg;

  lagopus_msg_info("****** cond_waint in\n");
  pthread_mutex_lock(&cond_lock);
  if (run == false) {
    pthread_cond_wait(&cond, &cond_lock);
  }
  pthread_mutex_unlock(&cond_lock);
  lagopus_msg_info("****** cond_waint out\n");
  /* Event loop. */
  while (run && (event = event_fetch(em)) != NULL) {
    lagopus_msg_info("****** event %p\n", event);
    event_call(event);
    event = NULL;
  }

  pthread_mutex_destroy(&cond_lock);
  pthread_cond_destroy(&cond);
  lagopus_msg_info("****** NULL out %p\n", event);
  return NULL;
}

void
setUp(void) {
  uint32_t on = 1;
  struct sockaddr_storage so = {0,0,{0}};
  struct sockaddr_in *sin = (struct sockaddr_in *)&so;

  if (s4 != -1) {
    return;
  }

  sin->sin_family = AF_INET;
  sin->sin_port = htons(10032);
  sin->sin_addr.s_addr = INADDR_ANY;

  s4 = socket(AF_INET, SOCK_STREAM, 0);
  if (s4 < 0) {
    perror("socket");
    exit(1);
  }

  setsockopt(s4, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  if (bind(s4, (struct sockaddr *) sin, sizeof(*sin)) < 0) {
    perror("bind");
    exit(1);
  }

  if (listen(s4, 5) < 0) {
    perror("listen");
    exit(1);
  }

  /* Event manager allocation. */
  em = event_manager_alloc();
  if (em == NULL) {
    fprintf(stderr, "event_manager_alloc fail.");
    exit(1);
  }

  /* init datapath. */
  TEST_ASSERT_EQUAL(dp_api_init(), LAGOPUS_RESULT_OK);

  /* interface. */
  interface_info.type = DATASTORE_INTERFACE_TYPE_ETHERNET_RAWSOCK;
  interface_info.eth_rawsock.port_number = 0;
  interface_info.eth_dpdk_phy.device = strdup("eth0");
  if (interface_info.eth_dpdk_phy.device == NULL) {
    TEST_FAIL_MESSAGE("device is NULL.");
  }
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK,
                    dp_interface_create(interface_name));
  dp_interface_info_set(interface_name, &interface_info);

  /* port. */
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK,
                    dp_port_create(port_name));
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK,
                    dp_port_interface_set(port_name, interface_name));

  /* bridge. */
  bridge_info.dpid = dpid;
  bridge_info.fail_mode = DATASTORE_BRIDGE_FAIL_MODE_SECURE;
  bridge_info.max_buffered_packets = UINT32_MAX;
  bridge_info.max_ports = UINT16_MAX;
  bridge_info.max_tables = UINT8_MAX;
  bridge_info.max_flows = UINT32_MAX;
  bridge_info.capabilities = UINT64_MAX;
  bridge_info.action_types = UINT64_MAX;
  bridge_info.instruction_types = UINT64_MAX;
  bridge_info.reserved_port_types = UINT64_MAX;
  bridge_info.group_types = UINT64_MAX;
  bridge_info.group_capabilities = UINT64_MAX;
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK,
                    dp_bridge_create(bridge_name, &bridge_info));
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK,
                    dp_bridge_port_set(bridge_name, port_name, 0));

  pthread_cond_init(&cond, NULL);

  pthread_create(&p, NULL, &loop, NULL);

  printf("setup end\n");
}
void
tearDown(void) {
  return;
  run = false;

  close(s4);
  event_manager_stop(em);

  pthread_join(p, NULL);

  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK,
                    dp_bridge_destroy(bridge_name));
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK,
                    dp_port_destroy(port_name));
  free((void *)interface_info.eth_dpdk_phy.device);
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK,
                    dp_interface_destroy(interface_name));

  channel_mgr_finalize();
  event_manager_free(em);
  dp_api_fini();
}

void
test_channel_mgr_channel_create(void) {
  lagopus_result_t ret;
  lagopus_ip_address_t *addr4 = NULL;

  channel_mgr_initialize(em);
  lagopus_ip_address_create("127.0.0.1", true, &addr4);
  ret = channel_mgr_channel_create("channel1", addr4, 10032, NULL, 0,
                                   DATASTORE_CHANNEL_PROTOCOL_TCP);
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);

  ret = channel_mgr_channel_start("channel1");
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_INVALID_ARGS, ret);

  ret = channel_mgr_channel_destroy("channel1");
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);

  lagopus_ip_address_destroy(addr4);
}

void
test_channel_mgr_apis_fail(void) {
  lagopus_result_t ret;
  struct channel *chan = NULL;

  ret = channel_mgr_channel_lookup_by_name("hoge", &chan);
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_NOT_FOUND, ret);
  TEST_ASSERT_EQUAL(NULL, chan);
}

void
test_channel_tcp(void) {
  int sock4;
  socklen_t size;
  struct sockaddr_in sin;
  lagopus_ip_address_t *addr4 = NULL, *laddr4 = NULL;
  struct channel *chan = NULL;
  lagopus_result_t ret;
  uint16_t sport;
  datastore_controller_role_t role;
  datastore_channel_status_t status;
  uint8_t version;

  printf("test_channel_tcp in\n");
  channel_mgr_initialize(em);
  lagopus_ip_address_create("127.0.0.1", true, &addr4);

  ret = channel_mgr_channel_create("channel1", addr4, 10032, addr4, 20032,
                                   DATASTORE_CHANNEL_PROTOCOL_TCP);
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);
  ret = channel_mgr_channel_lookup_by_name("channel1", &chan);
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);

  ret = channel_mgr_controller_set("channel1", DATASTORE_CONTROLLER_ROLE_MASTER,
                                   DATASTORE_CONTROLLER_CONNECTION_TYPE_AUXILIARY);
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);

  ret = channel_mgr_channel_local_port_get("channel1", &sport);
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);
  TEST_ASSERT_EQUAL(20032, sport);

  ret = channel_mgr_channel_local_addr_get("channel1", &laddr4);
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);
  TEST_ASSERT_EQUAL(true, lagopus_ip_address_equals(addr4, laddr4));

  ret = channel_mgr_channel_connection_status_get("channel1", &status);
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);
  TEST_ASSERT_EQUAL(DATASTORE_CHANNEL_DISONNECTED, status);

  ret = channel_mgr_channel_role_get("channel1", &role);
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);
  TEST_ASSERT_EQUAL(DATASTORE_CONTROLLER_ROLE_MASTER, role);

  ret = channel_mgr_ofp_version_get("channel1", &version);
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);
  TEST_ASSERT_EQUAL(4, version);

  ret = channel_mgr_channel_dpid_set("channel1", (uint64_t) dpid);
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);

  ret = channel_mgr_channel_dpid_unset("channel1");
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);

  ret = channel_mgr_channel_dpid_set("channel1", (uint64_t) dpid);
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);

  ret = channel_mgr_channel_start("channel1");
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);

  pthread_mutex_lock(&cond_lock);
  printf("cond broadcast\n");
  run = true;
  pthread_cond_broadcast(&cond);
  pthread_mutex_unlock(&cond_lock);
  size = sizeof(sin);
  printf("accept in\n");
  sock4 = accept(s4, (struct sockaddr *)&sin, &size);
  TEST_ASSERT_NOT_EQUAL(-1, sock4);

  ret = channel_mgr_channel_connection_status_get("channel1", &status);
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);
//  TEST_ASSERT_EQUAL(DATASTORE_CHANNEL_CONNECTED, status);

  ret = channel_mgr_channel_dpid_unset("channel1");
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_BUSY, ret);

  ret = channel_mgr_channel_stop("channel1");
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);

  ret = channel_mgr_channel_destroy("channel1");
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_OK, ret);

  ret = channel_mgr_channel_lookup_by_name("channel1", &chan);
  TEST_ASSERT_EQUAL(LAGOPUS_RESULT_NOT_FOUND, ret);


  close(sock4);
  lagopus_ip_address_destroy(addr4);
  lagopus_ip_address_destroy(laddr4);
}
