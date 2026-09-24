#include "btstack.h"
#include "btstack_run_loop_windows.h"
#include "btstack_tlv_windows.h"
#include "classic/btstack_link_key_db_tlv.h"
#include "ble/le_device_db_tlv.h"
#include "hci_transport_usb.h"
#include "hci_dump_windows_fs.h"
#include "audio/audio_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void headset_init(void);
void headset_command(const char * command);
static btstack_packet_callback_registration_t registration;
static btstack_tlv_windows_t database;
static btstack_timer_source_t timer;
static int working;
static int stopping;
static unsigned ticks;
static hci_dump_t filtered_dump;
static void log_control_packet(uint8_t type, uint8_t in, uint8_t * packet, uint16_t len) {
    // Keep HCI/control evidence without recording call audio payloads.
    if (type == HCI_SCO_DATA_PACKET) return;
    hci_dump_windows_fs_get_instance()->log_packet(type, in, packet, len);
}
void hal_led_toggle(void) {}

static void poll_control(btstack_timer_source_t * ts) {
    FILE * file = fopen("command.txt", "r");
    if (file) {
        char command[128] = {0};
        char * read = fgets(command, sizeof(command), file);
        fclose(file); remove("command.txt");
        if (read) {
            command[strcspn(command, "\r\n")] = 0;
            if (strcmp(command, "quit") == 0) { audio_stop(); stopping = 1; hci_power_control(HCI_POWER_OFF); }
            else headset_command(command);
        }
    }
    if (!working && !stopping && ++ticks > 150) {
        printf("[HCI] Startup timed out; run ax201_probe --hci to inspect firmware state\n");
        exit(2);
    }
    btstack_run_loop_set_timer(ts, 200);
    btstack_run_loop_add_timer(ts);
}
static void state_event(uint8_t type, uint16_t channel, uint8_t * packet, uint16_t size) {
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET || hci_event_packet_get_type(packet) != BTSTACK_EVENT_STATE) return;
    switch (btstack_event_state_get_state(packet)) {
        case HCI_STATE_WORKING: {
            bd_addr_t address; gap_local_bd_addr(address); working = 1;
            printf("[HCI] Controller ready (BTstack working)\n[BT] Local address: %s\n", bd_addr_to_str(address));
            printf("[BT] Discoverable as AX201 HFP Headset; open iPhone Bluetooth settings\n");
            break;
        }
        case HCI_STATE_OFF:
            if (stopping) { btstack_tlv_windows_deinit(&database); printf("[BT] Stopped\n"); exit(0); }
            break;
        default: break;
    }
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[APP] AX201 HFP-HF PoC; warm firmware required; WASAPI starts only on SCO\n");
    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_windows_get_instance());
    hci_dump_windows_fs_open("hci_dump.pklg", HCI_DUMP_PACKETLOGGER);
    filtered_dump = *hci_dump_windows_fs_get_instance();
    filtered_dump.log_packet = log_control_packet;
    hci_dump_init(&filtered_dump);
    hci_init(hci_transport_usb_instance(), NULL);
    const btstack_tlv_t * tlv = btstack_tlv_windows_init_instance(&database, "link-keys.tlv");
    btstack_tlv_set_instance(tlv, &database);
    hci_set_link_key_db(btstack_link_key_db_tlv_get_instance(tlv, &database));
    le_device_db_tlv_configure(tlv, &database);
    registration.callback = state_event; hci_add_event_handler(&registration);
    headset_init();
    btstack_run_loop_set_timer_handler(&timer, poll_control);
    btstack_run_loop_set_timer(&timer, 200); btstack_run_loop_add_timer(&timer);
    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
    return 0;
}
