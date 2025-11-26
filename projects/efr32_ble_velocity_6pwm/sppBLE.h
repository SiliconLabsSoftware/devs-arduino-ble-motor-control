#pragma once

#ifndef ARDUINO_SILABS_STACK_BLE_SILABS
  #error "This library is only compatible with the Silicon Labs BLE stack. Please select 'BLE (Silabs)' in 'Tools > Protocol stack'."
#endif

#include "Arduino.h"
extern "C" {
  #include "sl_bluetooth.h"
}
#include "api/RingBuffer.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <SimpleFOC.h>
#include <vector>

// SPP service UUID: 4880c12c-fdcb-4077-8920-a450d7f9b907
const uuid_128 spp_service_uuid = { 
  .data = { 0x07, 0xb9, 0xf9, 0xd7, 0x50, 0xa4, 0x20, 0x89, 0x77, 0x40, 0xcb, 0xfd, 0x2c, 0xc1, 0x80, 0x48, } };

// SPP data UUID: fec26ec4-6d71-4442-9f81-55bc21d658d6
const uuid_128 spp_data_characteristic_uuid = { 
  .data = { 0xd6, 0x58, 0xd6, 0x21, 0xbc, 0x55, 0x81, 0x9f, 0x42, 0x44, 0x71, 0x6d, 0xc4, 0x6e, 0xc2, 0xfe, } };

class sppBLEClass : public Stream {
public:
  sppBLEClass();

  virtual ~sppBLEClass() = default;

  // Log
  void enable_log(bool enable);
  bool log_is_enalbe();
  void set_log_tag(const char *tag);
  const char *get_log_tag();

  // Stream
  virtual int available() override;
  virtual int read() override;
  virtual int peek() override;
  virtual size_t write(uint8_t) override;
  virtual size_t write(const uint8_t *buffer, size_t size) override;

  void onCheckSendCondition(bool (*user_checksendcondition_callback)(size_t, const uint8_t*, size_t));

  // BLE
  virtual void begin(const char* ble_name = "motor");
  virtual void end();

  virtual void handle_ble_event(sl_bt_msg_t *evt);

  void onBLEEvent(void (*user_onbleevent_callback)(sl_bt_msg_t*));
  void onConnect(void (*user_onconnect_callback)(uint8_t));
  void onDisconnect(void (*user_ondisconnect_callback)(uint8_t));

  // BLE:GATT DB
  void set_ble_name(const char *ble_name);
  const char *get_ble_name();

  void set_name_show_uuid(bool enable);
  bool get_name_show_uuid();

  void onInitGATTDB(void (*user_oninitgattdb_callback)(uint16_t));

  // BLE:ADV
  void set_adv_discovery_mode(uint8_t discovery_mode);
  uint8_t get_adv_discovery_mode();

  void set_adv_connection_mode(uint8_t connection_mode);
  uint8_t get_adv_connection_mode();

  void set_adv_interval_min(uint32_t interval_min);
  uint32_t get_adv_interval_min();

  void set_adv_interval_max(uint32_t interval_max);
  uint32_t get_adv_interval_max();

  void set_adv_duration(uint16_t duration);
  uint16_t get_adv_duration();

  void set_adv_max_event(uint8_t max_event);
  uint8_t get_adv_max_event();

  // BLE: Connections
  void print_connections();

  // BLE:SPP
  virtual size_t send_data(uint8_t connection, uint16_t length, uint8_t *data);

  virtual size_t send_mesg(uint8_t connection, const char *message);

private:
  enum state {
    ST_NOT_STARTED = 0,
    ST_BOOT,
    ST_DISCONNECTED,
    ST_READY,
  };
  state _state{ST_NOT_STARTED};

  // Log
  static const uint8_t _printf_buffer_size = 64u;

  struct log_t {
    bool enable;
    const char *tag;
  };
  void log(const char* fmt, ...);

  log_t _log{false, "[sppBLE] "};

  // Stream
  bool (*user_checksendcondition_callback)(size_t, const uint8_t*, size_t);

  // BLE
  void handle_boot_event(sl_bt_msg_t *evt);
  void handle_conn_open(sl_bt_msg_t *evt);
  void handle_conn_close(sl_bt_msg_t *evt);
  void handle_gatt_data_receive(sl_bt_msg_t *evt);

  bool _ble_stack_booted;
  void (*user_onbleevent_callback)(sl_bt_msg_t*);
  void (*user_onconnect_callback)(uint8_t connection);
  void (*user_ondisconnect_callback)(uint8_t connection);

  // BLE:GATT DB
  struct gatt_db_t {
    bool initialized;
    uint16_t session_id;
    uint16_t generic_access_service_handle;
    uint16_t device_name_characteristic_handle;
    uint16_t spp_service_handle;
    uint16_t spp_data_characteristic_handle;
    const char* device_name;
    bool device_name_support_uuid;
  };

  void init_gattdb();

  static const size_t _max_ble_name_size = 512u;
  gatt_db_t _gatt_db{ false, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, "unknown", true };
  void (*user_oninitgattdb_callback)(uint16_t);

  // BLE:ADV
  struct adv_t {
    uint8_t handle;
    uint8_t disc_mode;
    uint8_t conn_mode;
    uint32_t interval_min;
    uint32_t interval_max;
    uint16_t duration;
    uint8_t max_event;
  };

  virtual void init_advertising();
  virtual void start_advertising();
  virtual void stop_advertising();

  adv_t _adv{0xFF, sl_bt_advertiser_general_discoverable, sl_bt_legacy_advertiser_connectable, 160, 160, 0, 0};

  // BLE:Connections
  struct connection_t {
    bool is_master;
    uint8_t conn;
    uint8_t bonding;
    bd_addr addr;

    connection_t(
      bool is_master,
      uint8_t conn,
      uint8_t bonding,
      const bd_addr &addr
    ) : is_master(is_master), conn(conn), bonding(bonding), addr(addr) {}
  };

  bool add_connection(
    bool is_master,
    uint8_t conn,
    uint8_t bonding_handle,
    const bd_addr &addr);

  void close_connection(
    uint8_t conn);

  std::vector<connection_t> _connections;

  // BLE:SPP
  static const uint16_t _max_ble_transfer_size = 250u;
  static const size_t _data_buffer_size = 512u;

  RingBufferN<_data_buffer_size> _rx_buf;
  RingBufferN<_data_buffer_size> _tx_buf;

  SemaphoreHandle_t _rx_buf_mutex;
  StaticSemaphore_t _rx_buf_mutex_buf;
  SemaphoreHandle_t _tx_buf_mutex;
  StaticSemaphore_t _tx_buf_mutex_buf;

  size_t transfer_outgoing_data(bool safe = true);
};

extern sppBLEClass sppBLE;