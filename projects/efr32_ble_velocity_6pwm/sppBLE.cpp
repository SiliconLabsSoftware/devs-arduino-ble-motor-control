#include "sppBLE.h"

using namespace arduino;

#define GATTDB_NON_ADVERTISED_SERVICE 0x00

sppBLEClass::sppBLEClass() :
  user_checksendcondition_callback(nullptr),
  user_onbleevent_callback(nullptr),
  user_onconnect_callback(nullptr),
  user_ondisconnect_callback(nullptr),
  user_oninitgattdb_callback(nullptr) {
  this->_rx_buf_mutex = xSemaphoreCreateMutexStatic(&this->_rx_buf_mutex_buf);
  configASSERT(this->_rx_buf_mutex);
  this->_tx_buf_mutex = xSemaphoreCreateMutexStatic(&this->_tx_buf_mutex_buf);
  configASSERT(this->_tx_buf_mutex);
}

int sppBLEClass::available() {
  return _rx_buf.available();
}

int sppBLEClass::read() {
  xSemaphoreTake(_rx_buf_mutex, portMAX_DELAY);
  int data = _rx_buf.read_char();
  xSemaphoreGive(_rx_buf_mutex);
  return data;
}

int sppBLEClass::peek() {
  return _rx_buf.peek();
}

size_t sppBLEClass::write(uint8_t data) {
  return write(&data, 1);
}

size_t sppBLEClass::write(const uint8_t *buffer, size_t size) {
  if (_tx_buf.isFull()) {
    log("Tx buffer overflow!");
    transfer_outgoing_data();
    return -1;
  }

  size_t i = 0;

  xSemaphoreTake(_tx_buf_mutex, portMAX_DELAY);
  for (; i < size; ++i) {
    _tx_buf.store_char(buffer[i]);
    if (user_checksendcondition_callback) {
      if (user_checksendcondition_callback(i, buffer, size)) {
        transfer_outgoing_data(false);
      }
    } else if (_tx_buf.isFull()) {
      xSemaphoreGive(_tx_buf_mutex);
      transfer_outgoing_data();
      return -1;
    } else {
      // MISRA
    }
  }
  xSemaphoreGive(_tx_buf_mutex);

  if (!user_checksendcondition_callback || user_checksendcondition_callback(i, buffer, size)) {
    transfer_outgoing_data();
  }

  return size;
}

void sppBLEClass::onCheckSendCondition(
  bool (*user_checksendcondition_callback)(size_t, const uint8_t*, size_t)
) {
  if (!user_checksendcondition_callback) return;
  this->user_checksendcondition_callback = user_checksendcondition_callback;
}

// BLE:GATT DB
void sppBLEClass::set_ble_name(const char *ble_name) {
  _gatt_db.device_name = ble_name;

  if (!_gatt_db.initialized) {
    return;
  }

  // If the GATT DB is already initialized we have to set the new name in the DB
  sl_status_t sc = sl_bt_gatt_server_write_attribute_value(
    _gatt_db.device_name_characteristic_handle,
    0u,
    strlen(ble_name),
    (const uint8_t*) ble_name);

  if (sc != SL_STATUS_OK) {
    log("Setting new advertised name in GATT DB failed!");
  }
}

const char *sppBLEClass::get_ble_name() {
  return _gatt_db.device_name;
}

void sppBLEClass::set_name_show_uuid(bool enable) {
  _gatt_db.device_name_support_uuid = enable;
}

bool sppBLEClass::get_name_show_uuid() {
  return _gatt_db.device_name_support_uuid;
}

void sppBLEClass::init_gattdb() {
  if (_gatt_db.initialized) return;

  sl_status_t sc;

  // Create a new GATT database
  sc = sl_bt_gattdb_new_session(&_gatt_db.session_id);
  app_assert_status(sc);

  // Add the Generic Access service to the GATT DB
  const uint8_t generic_access_service_uuid[] = { 0x00, 0x18 };
  sc = sl_bt_gattdb_add_service(
    _gatt_db.session_id,
    sl_bt_gattdb_primary_service,
    SL_BT_GATTDB_ADVERTISED_SERVICE,
    sizeof(generic_access_service_uuid),
    generic_access_service_uuid,
    &_gatt_db.generic_access_service_handle);
  app_assert_status(sc);

  char dev_name[_max_ble_name_size] = { 0 };

  if (_gatt_db.device_name_support_uuid) {
    bd_addr address;
    uint8_t address_type;

    sc = sl_bt_system_get_identity_address(&address, &address_type);
    app_assert_status(sc);

    snprintf(dev_name, sizeof(dev_name), "%s_%02x%02x%02x",
              _gatt_db.device_name,
              address.addr[2],
              address.addr[1],
              address.addr[0]);
  } else {
    strncpy(dev_name, _gatt_db.device_name, strlen(_gatt_db.device_name));
    dev_name[strlen(_gatt_db.device_name)] = '\0';
  }

  log("BLE device name:%s", dev_name);

  // Add the Device Name characteristic to the Generic Access service
  // The value of the Device Name characteristic will be advertised
  const sl_bt_uuid_16_t device_name_characteristic_uuid = { .data = { 0x00, 0x2A } };
  sc = sl_bt_gattdb_add_uuid16_characteristic(
    _gatt_db.session_id,
    _gatt_db.generic_access_service_handle,
    SL_BT_GATTDB_CHARACTERISTIC_READ,
    0x00,
    0x00,
    device_name_characteristic_uuid,
    sl_bt_gattdb_fixed_length_value,
    strlen(dev_name),
    strlen(dev_name),
    (const uint8_t*) dev_name,
    &_gatt_db.device_name_characteristic_handle);
  app_assert_status(sc);

  // Start the Generic Access service
  sc = sl_bt_gattdb_start_service(
    _gatt_db.session_id,
    _gatt_db.generic_access_service_handle);
  app_assert_status(sc);

  // Add the SPP service to the GATT DB
  // UUID: 4880c12c-fdcb-4077-8920-a450d7f9b907
  sc = sl_bt_gattdb_add_service(
    _gatt_db.session_id,
    sl_bt_gattdb_primary_service,
    GATTDB_NON_ADVERTISED_SERVICE,
    sizeof(spp_service_uuid),
    spp_service_uuid.data,
    &_gatt_db.spp_service_handle);
  app_assert_status(sc);

  // Add the 'SPP Data' characteristic to the SPP service
  // UUID: fec26ec4-6d71-4442-9f81-55bc21d658d6
  uint8_t spp_data_char_init_value = 0;
  sc = sl_bt_gattdb_add_uuid128_characteristic(
    _gatt_db.session_id,
    _gatt_db.spp_service_handle,
    SL_BT_GATTDB_CHARACTERISTIC_WRITE_NO_RESPONSE | SL_BT_GATTDB_CHARACTERISTIC_NOTIFY,
    0x00,
    0x00,
    spp_data_characteristic_uuid,
    sl_bt_gattdb_fixed_length_value,
    _max_ble_transfer_size,             // max length
    sizeof(spp_data_char_init_value),   // initial value length
    &spp_data_char_init_value,          // initial value
    &_gatt_db.spp_data_characteristic_handle);
  app_assert_status(sc);

  // Start the SPP service
  sc = sl_bt_gattdb_start_service(
    _gatt_db.session_id,
    _gatt_db.spp_service_handle);
  app_assert_status(sc);

  if (user_oninitgattdb_callback) {
    user_oninitgattdb_callback(_gatt_db.session_id);
  }

  // Commit the GATT DB changes
  sc = sl_bt_gattdb_commit(_gatt_db.session_id);
  app_assert_status(sc);

  _gatt_db.initialized = true;
}

void sppBLEClass::onInitGATTDB(void (*user_oninitgattdb_callback)(uint16_t)) {
  if (!user_oninitgattdb_callback) return;
  this->user_oninitgattdb_callback = user_oninitgattdb_callback;
}

// BLE:ADV
void sppBLEClass::set_adv_discovery_mode(uint8_t discovery_mode) {
  _adv.disc_mode = discovery_mode;
}

uint8_t sppBLEClass::get_adv_discovery_mode() {
  return _adv.disc_mode;
}

void sppBLEClass::set_adv_connection_mode(uint8_t connection_mode) {
  _adv.conn_mode = connection_mode;
}

uint8_t sppBLEClass::get_adv_connection_mode() {
  return _adv.conn_mode;
}

void sppBLEClass::set_adv_interval_min(uint32_t interval_min) {
  _adv.interval_min = interval_min;
}

uint32_t sppBLEClass::get_adv_interval_min() {
  return _adv.interval_min;
}

void sppBLEClass::set_adv_interval_max(uint32_t interval_max) {
  _adv.interval_max = interval_max;
}

uint32_t sppBLEClass::get_adv_interval_max() {
  return _adv.interval_max;
}

void sppBLEClass::set_adv_duration(uint16_t duration) {
  _adv.duration = duration;
}

uint16_t sppBLEClass::get_adv_duration() {
  return _adv.duration;
}

void sppBLEClass::set_adv_max_event(uint8_t max_event) {
  _adv.max_event = max_event;
}

uint8_t sppBLEClass::get_adv_max_event() {
  return _adv.max_event;
}

void sppBLEClass::init_advertising() {
  sl_status_t sc;

  if (_adv.handle != SL_BT_INVALID_ADVERTISING_SET_HANDLE) {
    sc = sl_bt_advertiser_delete_set(_adv.handle);
    app_assert_status(sc);
  }

  sc = sl_bt_advertiser_create_set(&_adv.handle);
  app_assert_status(sc);

  sc = sl_bt_advertiser_set_timing(_adv.handle,
                                   _adv.interval_min,
                                   _adv.interval_max,
                                   _adv.duration,
                                   _adv.max_event);
  app_assert_status(sc);
}

void sppBLEClass::start_advertising() {
  if (_adv.handle == SL_BT_INVALID_ADVERTISING_SET_HANDLE) return;

  sl_status_t sc;

  sc = sl_bt_legacy_advertiser_generate_data(_adv.handle,
                                             _adv.disc_mode);
  app_assert_status(sc);

  sc = sl_bt_legacy_advertiser_start(_adv.handle,
                                     _adv.conn_mode);
  app_assert_status(sc);
}

void sppBLEClass::stop_advertising() {
  if (_adv.handle == SL_BT_INVALID_ADVERTISING_SET_HANDLE) return;
  sl_status_t sc = sl_bt_advertiser_stop(_adv.handle);
  app_assert_status(sc);
  sc = sl_bt_advertiser_delete_set(_adv.handle);
  app_assert_status(sc);
}

// BLE: Connections
void sppBLEClass::print_connections() {
  for (const auto & it : _connections) {
    log("conn:0x%02X bonding:0x%02X %s addr:%02X:%02X:%02X:%02X:%02X:%02X\n",
        it.conn,
        it.bonding,
        it.is_master ? "master" : "slave",
        it.addr.addr[5],
        it.addr.addr[4],
        it.addr.addr[3],
        it.addr.addr[2],
        it.addr.addr[1],
        it.addr.addr[0]);
  }
}

bool sppBLEClass::add_connection(
  bool is_master,
  uint8_t conn,
  uint8_t bonding,
  const bd_addr &addr
) {
  if (_connections.size() >= SL_BT_CONFIG_MAX_CONNECTIONS) return false;

  _connections.emplace_back(is_master, conn, bonding, addr);

  return true;
}

void sppBLEClass::close_connection(
  uint8_t conn
) {
  for (auto it = _connections.begin(); it != _connections.end(); ++it) {
    if (it->conn == conn) {
      _connections.erase(it);
      return;
    }
  }
  return;
}

// BLE:SPP
size_t sppBLEClass::send_data(
  uint8_t conn,
  uint16_t length,
  uint8_t *data
) {
  sl_status_t sc;
  if (conn == 0xFF) {
    sc = sl_bt_gatt_server_notify_all(_gatt_db.spp_data_characteristic_handle, 
                                      length,
                                      data);
  } else {
    sc = sl_bt_gatt_server_send_notification(conn,
                                             _gatt_db.spp_data_characteristic_handle,
                                             length,
                                             data);
  }

  if (sc == SL_STATUS_OK) return length;
  return 0;
}

size_t sppBLEClass::send_mesg(
  uint8_t conn,
  const char *message
) {
  return send_data(conn, strlen(message), (uint8_t *)message);
}

size_t sppBLEClass::transfer_outgoing_data(bool safe) {
  uint8_t local_buf[_max_ble_transfer_size];
  size_t local_buf_idx = 0;

  if (safe) xSemaphoreTake(_tx_buf_mutex, portMAX_DELAY);

  for (; local_buf_idx < _max_ble_transfer_size && _tx_buf.available(); ++local_buf_idx) {
    local_buf[local_buf_idx] = _tx_buf.read_char();
  }

  if (safe) xSemaphoreGive(_tx_buf_mutex);

  return send_data(0xFF, local_buf_idx, local_buf);
}

// Log
void sppBLEClass::enable_log(bool enable) {
  _log.enable = enable;
}

bool sppBLEClass::log_is_enalbe() {
  return _log.enable;
}

void sppBLEClass::set_log_tag(const char *tag) {
  _log.tag = tag;
}

const char *sppBLEClass::get_log_tag() {
  return _log.tag;
}

void sppBLEClass::log(const char* fmt, ...) {
  if (!_log.enable) return;

  char message[_printf_buffer_size];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);
  Serial.print(_log.tag);
  Serial.println(message);
}

// BLE
void sppBLEClass::onBLEEvent(void (*user_onbleevent_callback)(sl_bt_msg_t*)) {
  if (!user_onbleevent_callback) return;
  this->user_onbleevent_callback = user_onbleevent_callback;
}

void sppBLEClass::onConnect(void (*user_onconnect_callback)(uint8_t)) {
  if (!user_onconnect_callback) return;
  this->user_onconnect_callback = user_onconnect_callback;
}

void sppBLEClass::onDisconnect(void (*user_ondisconnect_callback)(uint8_t)) {
  if (!user_ondisconnect_callback) return;
  this->user_ondisconnect_callback = user_ondisconnect_callback;
}

void sppBLEClass::handle_boot_event(sl_bt_msg_t *evt) {
  if (!evt) return;

  log("BLE stack booted");

  _ble_stack_booted = true;
  if (_state == state::ST_BOOT) {
    init_advertising();
    start_advertising();
    _state = state::ST_DISCONNECTED;
  }
}

void sppBLEClass::handle_conn_open(sl_bt_msg_t *evt) {
  if (!evt) return;

  sl_bt_evt_connection_opened_t *ev_conn = &evt->data.evt_connection_opened;

  log("BLE connection 0x%02X opened", ev_conn->connection);

  if (!add_connection(false,
                      ev_conn->connection,
                      ev_conn->bonding,
                      ev_conn->address)) return;

  if (_connections.size() < SL_BT_CONFIG_MAX_CONNECTIONS) {
    start_advertising();
  }

  _state = state::ST_READY;

  if (user_onconnect_callback) {
    user_onconnect_callback(ev_conn->connection);
  }
}

void sppBLEClass::handle_conn_close(sl_bt_msg_t *evt) {
  if (!evt) return;

  sl_bt_evt_connection_closed_t *ev_conn = &evt->data.evt_connection_closed;

  log("BLE connection 0x%02X closed", ev_conn->connection);

  close_connection(ev_conn->connection);

  if (_connections.empty()) {
    _state = state::ST_DISCONNECTED;
  } else if (_connections.size() < SL_BT_CONFIG_MAX_CONNECTIONS) {
    start_advertising();
  }

  if (user_ondisconnect_callback) {
    user_ondisconnect_callback(ev_conn->connection);
  }
}

void sppBLEClass::handle_gatt_data_receive(sl_bt_msg_t *evt) {
  if (!evt) return;

  if (_gatt_db.spp_data_characteristic_handle 
    != evt->data.evt_gatt_server_attribute_value.attribute) return;

  log("GATT data received");

  uint8_t data_len = evt->data.evt_gatt_server_attribute_value.value.len;
  uint8_t *data = evt->data.evt_gatt_server_attribute_value.value.data;

  if (_rx_buf.isFull()) {
    // Overflow, Rx buffer is full, cannot store any additional data
    log("Rx buffer overflow!");
    return;
  }

  xSemaphoreTake(_rx_buf_mutex, portMAX_DELAY);

  uint8_t buffered_bytes = 0u;
  for (uint8_t i = 0; i < data_len; i++) {
    _rx_buf.store_char(data[i]);
    buffered_bytes++;
    if (_rx_buf.isFull()) {
      // Overflow, Rx buffer is full, cannot store any additional data
      xSemaphoreGive(_rx_buf_mutex);
      log("Rx buffer overflow!");
      return;
    }
  }

  xSemaphoreGive(_rx_buf_mutex);
}

void sppBLEClass::handle_ble_event(sl_bt_msg_t *evt) {
  if (user_onbleevent_callback) {
    user_onbleevent_callback(evt);
  }

  switch (SL_BT_MSG_ID(evt->header)) {
    case sl_bt_evt_system_boot_id:
      handle_boot_event(evt);
      break;

    case sl_bt_evt_connection_opened_id:
      handle_conn_open(evt);
      break;

    case sl_bt_evt_connection_closed_id:
      handle_conn_close(evt);
      break;

    case sl_bt_evt_gatt_server_attribute_value_id:
      handle_gatt_data_receive(evt);
      break;

    default:
      log("BLE event: 0x%x", SL_BT_MSG_ID(evt->header));
      break;
  }
}

void sppBLEClass::begin(const char* ble_name) {
  if (_state != state::ST_NOT_STARTED) return;

  set_ble_name(ble_name);
  init_gattdb();

  if (_ble_stack_booted) {
    init_advertising();
    start_advertising();
    _state = state::ST_DISCONNECTED;
  } else {
    _state = state::ST_BOOT;
  }
}

void sppBLEClass::end() {
  xSemaphoreTake(_tx_buf_mutex, portMAX_DELAY);
  xSemaphoreTake(_rx_buf_mutex, portMAX_DELAY);
  
  stop_advertising();

  // Close all connection
  if (_state == state::ST_READY) {
    sl_status_t sc;
    for (const auto & it : _connections) {
      sc = sl_bt_connection_close(it.conn);
      if (sc != SL_STATUS_OK) {
        xSemaphoreGive(_tx_buf_mutex);
        xSemaphoreGive(_rx_buf_mutex);
        log("Could not close connection");
        return;
      }
    }
    _connections.clear();
  }

  _rx_buf.clear();
  _tx_buf.clear();

  _state = state::ST_NOT_STARTED;
}

sppBLEClass sppBLE;

/**************************************************************************//**
 * Bluetooth stack event handler
 * Called when an event happens on BLE the stack
 *
 * @param[in] evt Event coming from the Bluetooth stack
 *****************************************************************************/
void sl_bt_on_event(sl_bt_msg_t* evt) {
  // Pass all the stack events to ezBLE
  sppBLE.handle_ble_event(evt);
}