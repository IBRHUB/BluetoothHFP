// Profile setup follows BTstack hfp_hf_demo's public API sequence.
// Audio starts only after the successful HFP audio-connection event.
#include "btstack.h"
#include "audio/audio_bridge.h"
#include "hfp_audio_codec.h"
#include "a2dp_sink.h"
#include "app/control.h"
#include "audio/media_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t sdp_record[256];
static uint8_t codecs[] = {HFP_CODEC_CVSD, HFP_CODEC_MSBC, HFP_CODEC_LC3_SWB};
static hci_con_handle_t acl = HCI_CON_HANDLE_INVALID, sco = HCI_CON_HANDLE_INVALID;
static hci_con_handle_t peer_acl = HCI_CON_HANDLE_INVALID;
static btstack_packet_callback_registration_t registration;
static unsigned received, transmitted;

void headset_command(const char * command) {
    uint8_t status = 0;
    if (strcmp(command, "scan") == 0) status = gap_inquiry_start(5);
    else if (strcmp(command, "disconnect") == 0 && peer_acl != HCI_CON_HANDLE_INVALID) status = gap_disconnect(peer_acl);
    else if (strncmp(command, "forget ", 7) == 0) {
        bd_addr_t address;
        if (peer_acl != HCI_CON_HANDLE_INVALID || !sscanf_bd_addr(command + 7, address)) status = 12;
        else { gap_drop_link_key_for_bd_addr(address); control_event("forgotten", command + 7, 0); }
    }
    else if (strncmp(command, "mic ", 4) == 0) {
        unsigned mute, gain;
        if (sscanf(command + 4, "%u %u", &mute, &gain) == 2 && mute <= 1 && gain <= 100) audio_controls((int)mute, gain);
        else status = 18;
    }
    else if (strncmp(command, "volume ", 7) == 0) audio_output_volume((unsigned)atoi(command + 7));
    else if (strcmp(command, "audio") == 0 && acl != HCI_CON_HANDLE_INVALID)
        status = hfp_hf_establish_audio_connection(acl);
    else if (strcmp(command, "audio-off") == 0 && acl != HCI_CON_HANDLE_INVALID)
        status = hfp_hf_release_audio_connection(acl);
    else if (strncmp(command, "connect ", 8) == 0) {
        bd_addr_t address;
        if (!sscanf_bd_addr(command + 8, address)) { printf("[APP] Invalid address\n"); return; }
        status = hfp_hf_establish_service_level_connection(address);
    } else { printf("[APP] Command unavailable (audio, audio-off, connect <BD_ADDR>, quit)\n"); return; }
    printf("[APP] %s request status=0x%02x\n", command, status);
    control_event("command", command, status);
}
static void sco_send(void) {
    if (sco == HCI_CON_HANDLE_INVALID) return;
    const int length = hci_get_sco_packet_length_for_connection(sco);
    if (length <= 3 || length > 258) return;
    hci_reserve_packet_buffer();
    uint8_t * packet = hci_get_outgoing_packet_buffer();
    little_endian_store_16(packet, 0, sco);
    packet[2] = (uint8_t)(length - 3);
    hfp_audio_codec_fill(packet + 3, (unsigned)(length - 3));
    hci_send_sco_packet_buffer(length);
    if (++transmitted == 1 || transmitted % 1000 == 0)
        printf("[SCO] TX queued=%u (WASAPI -> codec -> SCO)\n", transmitted);
    hci_request_sco_can_send_now_event_for_con_handle(sco);
}
static void hci_event(uint8_t type, uint16_t channel, uint8_t * packet, uint16_t size) {
    (void)channel;
    if (type == HCI_SCO_DATA_PACKET) {
        if (size >= 3 && (little_endian_read_16(packet, 0) & 0x0fff) == sco && size == packet[2] + 3) {
            hfp_audio_codec_receive(packet, size);
            if (++received == 1 || received % 1000 == 0) printf("[SCO] RX packets=%u bytes=%u\n", received, size);
        }
        return;
    }
    if (type != HCI_EVENT_PACKET) return;
    bd_addr_t address;
    switch (hci_event_packet_get_type(packet)) {
        case GAP_EVENT_INQUIRY_RESULT: {
            gap_event_inquiry_result_get_bd_addr(packet, address);
            control_event("device", bd_addr_to_str(address), 0);
            break;
        }
        case GAP_EVENT_INQUIRY_COMPLETE: control_event("scanComplete", "", 0); break;
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            if (hci_event_disconnection_complete_get_connection_handle(packet) == peer_acl) {
                peer_acl = HCI_CON_HANDLE_INVALID; control_event("disconnected", "", 0);
            }
            break;
        case HCI_EVENT_CONNECTION_COMPLETE:
            hci_event_connection_complete_get_bd_addr(packet, address);
            printf("[BT] ACL connection peer=%s status=0x%02x\n", bd_addr_to_str(address), hci_event_connection_complete_get_status(packet));
            if (!hci_event_connection_complete_get_status(packet)) peer_acl = hci_event_connection_complete_get_connection_handle(packet);
            control_event("peer", bd_addr_to_str(address), hci_event_connection_complete_get_status(packet));
            break;
        case HCI_EVENT_USER_CONFIRMATION_REQUEST:
            hci_event_user_confirmation_request_get_bd_addr(packet, address);
            printf("[BT] Headset Just Works pairing request\n");
            gap_ssp_confirmation_response(address);
            break;
        case HCI_EVENT_SIMPLE_PAIRING_COMPLETE:
            control_event("paired", "", hci_event_simple_pairing_complete_get_status(packet));
            printf("[BT] Pairing complete status=0x%02x\n", hci_event_simple_pairing_complete_get_status(packet));
            break;
        case HCI_EVENT_AUTHENTICATION_COMPLETE:
            printf("[BT] Authentication complete status=0x%02x\n", hci_event_authentication_complete_get_status(packet));
            break;
        case HCI_EVENT_PIN_CODE_REQUEST:
            hci_event_pin_code_request_get_bd_addr(packet, address); gap_pin_code_response(address, "0000"); break;
        case HCI_EVENT_SCO_CAN_SEND_NOW: sco_send(); break;
        case HCI_EVENT_SYNCHRONOUS_CONNECTION_COMPLETE:
            printf("[SCO] HCI synchronous complete status=0x%02x handle=0x%04x\n",
                hci_event_synchronous_connection_complete_get_status(packet), hci_event_synchronous_connection_complete_get_handle(packet));
            break;
        default: break;
    }
}
static void profile_event(uint8_t type, uint16_t channel, uint8_t * event, uint16_t size) {
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET || hci_event_packet_get_type(event) != HCI_EVENT_HFP_META) return;
    uint8_t status;
    switch (hci_event_hfp_meta_get_subevent_code(event)) {
        case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_ESTABLISHED:
            status = hfp_subevent_service_level_connection_established_get_status(event);
            control_event("hfp", "connected", status);
            if (status) { printf("[HFP] SLC failed status=0x%02x\n", status); break; }
            acl = hfp_subevent_service_level_connection_established_get_acl_handle(event);
            printf("[HFP] SLC established handle=0x%04x\n", acl);
            printf("[SCO] Waiting for iPhone call audio route (or command: audio)\n");
            break;
        case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_RELEASED:
            control_event("hfp", "closed", 0);
            audio_stop();
            hfp_audio_codec_stop();
            if (sco != HCI_CON_HANDLE_INVALID) headset_media_call_active(0);
            acl = HCI_CON_HANDLE_INVALID; sco = HCI_CON_HANDLE_INVALID;
            printf("[HFP] SLC released\n"); break;
        case HFP_SUBEVENT_AUDIO_CONNECTION_ESTABLISHED:
            status = hfp_subevent_audio_connection_established_get_status(event);
            control_event("call", "codec", status ? -status : hfp_subevent_audio_connection_established_get_negotiated_codec(event));
            if (status) { printf("[SCO] Audio link failed status=0x%02x\n", status); break; }
            sco = hfp_subevent_audio_connection_established_get_sco_handle(event);
            received = transmitted = 0;
            printf("[SCO] Audio link established handle=0x%04x codec=%u\n", sco,
                hfp_subevent_audio_connection_established_get_negotiated_codec(event));
            const unsigned rate = hfp_audio_codec_start(hfp_subevent_audio_connection_established_get_negotiated_codec(event));
            if (!rate) { printf("[AUDIO] Unsupported negotiated codec\n"); hfp_hf_release_audio_connection(acl); break; }
            headset_media_call_active(1);
            audio_start(rate);
            hci_request_sco_can_send_now_event_for_con_handle(sco); break;
        case HFP_SUBEVENT_AUDIO_CONNECTION_RELEASED:
            control_event("call", "stopped", 0);
            audio_stop();
            hfp_audio_codec_stop();
            headset_media_call_active(0);
            sco = HCI_CON_HANDLE_INVALID; printf("[SCO] Audio link released\n"); break;
        default: break;
    }
}
void headset_init(void) {
    l2cap_init(); rfcomm_init(); sdp_init(); sm_init();
    headset_media_init();
    const char * codec_option = getenv("AX201_HFP_CODEC");
    const uint8_t codec_count = codec_option && strcmp(codec_option, "CVSD") == 0 ? 1 :
        (codec_option && strcmp(codec_option, "mSBC") == 0 ? 2 : 3);
    const uint16_t features = (1 << HFP_HFSF_ESCO_S4) | (1 << HFP_HFSF_REMOTE_VOLUME_CONTROL) |
        (codec_count >= 2 ? (1 << HFP_HFSF_CODEC_NEGOTIATION) : 0);
    hfp_hf_init(1);
    hfp_hf_init_supported_features(features);
    hfp_hf_init_codecs(codec_count, codecs);
    hfp_hf_register_packet_handler(profile_event);
    hfp_hf_create_sdp_record_with_codecs(sdp_record, sdp_create_service_record_handle(), 1,
        "AX201 Hands-Free", features, codec_count, codecs);
    if (de_get_len(sdp_record) > sizeof(sdp_record) || sdp_register_service(sdp_record) != ERROR_CODE_SUCCESS) {
        printf("[HFP] SDP registration failed\n"); return;
    }
    printf("[HFP] Service registered as HF uuid=0x111E RFCOMM=1 codecs=%s\n",
        codec_count == 3 ? "CVSD,mSBC,LC3-SWB" : codec_count == 2 ? "CVSD,mSBC" : "CVSD");
    gap_set_local_name("AX201 HFP Headset");
    gap_set_class_of_device(0x200408);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_set_bondable_mode(1);
    gap_discoverable_control(1);
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_allow_role_switch(true);
    registration.callback = hci_event; hci_add_event_handler(&registration);
    hci_register_sco_packet_handler(hci_event);
}
