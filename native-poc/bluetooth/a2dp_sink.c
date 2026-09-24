#include "btstack.h"
#include "classic/btstack_sbc_bluedroid.h"
#include "a2dp_sink.h"
#include "audio/media_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aacdecoder_lib.h"
#include "app/control.h"
static uint8_t capabilities[] = {0x3f, 0xff, 2, 53}; // 44.1/48kHz, all SBC modes.
// MPEG-2/4 AAC-LC; 44.1/48 kHz stereo; VBR up to 320 kbit/s.
static uint8_t aac_capabilities[] = {0xc0, 0x01, 0x84, 0x84, 0xe2, 0x00};
static uint8_t aac_configuration[6];
static HANDLE_AACDECODER aac_decoder;
static uint8_t active_codec = AVDTP_CODEC_SBC, aac_seid;
static unsigned aac_errors;
static uint8_t configuration[4], sdp_media[256], sdp_control[256], sdp_target[256];
static btstack_sbc_decoder_bluedroid_t decoder_state;
static const btstack_sbc_decoder_t* decoder;
static unsigned rate, channels, packets;
static int streaming, call_active;
static uint8_t local_seid;
static void decoded(int16_t* data, int frames, int count_channels, int sample_rate, void* context) {
    (void)context;
    if (!streaming || call_active || sample_rate != (int)rate || frames <= 0) return;
    if (count_channels == 2) media_render_write(data, (unsigned)frames);
    else if (count_channels == 1 && frames <= 128) {
        int16_t stereo[256];
        for (int i = 0; i < frames; ++i) stereo[2*i] = stereo[2*i+1] = data[i];
        media_render_write(stereo, (unsigned)frames);
    }
}
static void reset_decoder(void) {
    if (aac_decoder) { aacDecoder_Close(aac_decoder); aac_decoder = NULL; }
    if (active_codec == AVDTP_CODEC_MPEG_2_4_AAC) {
        aac_decoder = aacDecoder_Open(TT_MP4_LATM_MCP1, 1);
        if (!aac_decoder) { printf("[A2DP] AAC decoder allocation failed\n"); return; }
        aac_errors = 0;
        return;
    }
    decoder = btstack_sbc_decoder_bluedroid_init_instance(&decoder_state);
    decoder->configure(&decoder_state, SBC_MODE_STANDARD, decoded, NULL);
}
void headset_media_call_active(int active) {
    call_active = active;
    if (active) { media_render_stop(); printf("[A2DP] Call has playback priority\n"); }
    else if (streaming) { reset_decoder(); media_render_start(rate); }
}
void headset_media_shutdown(void) {
    streaming = 0; media_render_stop();
    if (aac_decoder) { aacDecoder_Close(aac_decoder); aac_decoder = NULL; }
}
static void media_packet(uint8_t seid, uint8_t* packet, uint16_t size) {
    if ((seid != local_seid && seid != aac_seid) || !streaming || call_active || size < 13) return;
    // Standard RTP: CSRC list, optional extension and padding are bounds checked.
    if ((packet[0] >> 6) != 2) return;
    unsigned offset = 12 + 4 * (packet[0] & 15), end = size;
    if (offset >= end) return;
    if (packet[0] & 0x10) {
        if (offset + 4 > end) return;
        offset += 4 + 4 * big_endian_read_16(packet, offset + 2);
        if (offset >= end) return;
    }
    if (packet[0] & 0x20) {
        unsigned padding = packet[end - 1];
        if (!padding || padding >= end - offset) return;
        end -= padding;
    }
    if (active_codec == AVDTP_CODEC_MPEG_2_4_AAC) {
        if (!aac_decoder) return;
        UCHAR* bytes = packet + offset;
        UINT length = end - offset, valid = length;
        AAC_DECODER_ERROR error = aacDecoder_Fill(aac_decoder, &bytes, &length, &valid);
        if (error != AAC_DEC_OK || valid) { if (++aac_errors < 10) printf("[AAC] Fill error=0x%x remaining=%u\n", error, valid); return; }
        for (unsigned frame = 0; frame < 8; ++frame) {
            INT_PCM pcm[4096];
            error = aacDecoder_DecodeFrame(aac_decoder, pcm, 4096, 0);
            if (error == AAC_DEC_NOT_ENOUGH_BITS) break;
            if (!IS_OUTPUT_VALID(error)) { if (++aac_errors < 10) printf("[AAC] Decode error=0x%x\n", error); break; }
            CStreamInfo* info = aacDecoder_GetStreamInfo(aac_decoder);
            if (info->sampleRate != (int)rate || info->frameSize <= 0 || info->frameSize > 2048 || info->numChannels != 2) {
                printf("[AAC] Unexpected PCM format\n"); break;
            }
            media_render_write(pcm, (unsigned)info->frameSize);
        }
        if (++packets == 1 || packets % 500 == 0) printf("[A2DP] AAC media packets=%u decodeErrors=%u\n", packets, aac_errors);
        return;
    }
    const uint8_t header = packet[offset++];
    // SBC frames at negotiated max bitpool fit MTU; unsupported fragments are dropped.
    if ((header & 0xf0) || !(header & 15) || offset >= end) return;
    decoder->decode_signed_16(&decoder_state, 0, packet + offset, (uint16_t)(end - offset));
    if (++packets == 1 || packets % 500 == 0) printf("[A2DP] Media packets=%u\n", packets);
}
static void profile(uint8_t type, uint16_t channel, uint8_t* packet, uint16_t size) {
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET || hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) return;
    switch (packet[2]) {
        case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:
            control_event("mediaCodec", "SBC", 0);
            active_codec = AVDTP_CODEC_SBC;
            rate = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(packet);
            channels = a2dp_subevent_signaling_media_codec_sbc_configuration_get_num_channels(packet);
            printf("[A2DP] SBC configured rate=%u channels=%u bitpool=%u..%u\n", rate, channels,
                a2dp_subevent_signaling_media_codec_sbc_configuration_get_min_bitpool_value(packet),
                a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(packet)); break;
        case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_MPEG_AAC_CONFIGURATION:
            control_event("mediaCodec", "AAC", 0);
            active_codec = AVDTP_CODEC_MPEG_2_4_AAC;
            rate = a2dp_subevent_signaling_media_codec_mpeg_aac_configuration_get_sampling_frequency(packet);
            channels = a2dp_subevent_signaling_media_codec_mpeg_aac_configuration_get_num_channels(packet);
            printf("[A2DP] AAC configured rate=%u channels=%u bitrate=%lu VBR=%u\n", rate, channels,
                (unsigned long)a2dp_subevent_signaling_media_codec_mpeg_aac_configuration_get_bit_rate(packet),
                a2dp_subevent_signaling_media_codec_mpeg_aac_configuration_get_vbr(packet)); break;
        case A2DP_SUBEVENT_STREAM_ESTABLISHED:
            printf("[A2DP] Stream connection status=0x%02x\n", a2dp_subevent_stream_established_get_status(packet)); break;
        case A2DP_SUBEVENT_STREAM_STARTED:
            control_event("media", "playing", (int)rate);
            if ((rate != 44100 && rate != 48000) || (channels != 1 && channels != 2)) { printf("[A2DP] Unsupported configuration\n"); break; }
            streaming = 1; packets = 0; reset_decoder();
            if (!call_active) media_render_start(rate);
            printf("[A2DP] Stream started\n"); break;
        case A2DP_SUBEVENT_STREAM_SUSPENDED:
        case A2DP_SUBEVENT_STREAM_RELEASED:
        case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
            control_event("media", "stopped", 0);
            headset_media_shutdown(); printf("[A2DP] Stream suspended/released\n"); break;
        default: break;
    }
}
static void remote(uint8_t type, uint16_t channel, uint8_t* packet, uint16_t size) {
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET || hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
    if (packet[2] == AVRCP_SUBEVENT_CONNECTION_ESTABLISHED)
        printf("[AVRCP] Connection status=0x%02x\n", avrcp_subevent_connection_established_get_status(packet));
    if (packet[2] == AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED) {
        const uint8_t volume = avrcp_subevent_notification_volume_changed_get_absolute_volume(packet);
        media_render_volume(volume); printf("[AVRCP] Media volume=%u/127\n", volume);
    }
}
static void register_record(uint8_t* record, unsigned capacity) {
    if (de_get_len(record) > capacity || sdp_register_service(record) != ERROR_CODE_SUCCESS) {
        printf("[A2DP] SDP registration failed\n"); exit(3);
    }
}
void headset_media_init(void) {
    a2dp_sink_init(); a2dp_sink_register_packet_handler(profile); a2dp_sink_register_media_handler(media_packet);
    // Advertise AAC before mandatory SBC; phone retains final codec selection.
    avdtp_stream_endpoint_t* aac_endpoint = a2dp_sink_create_stream_endpoint(AVDTP_AUDIO, AVDTP_CODEC_MPEG_2_4_AAC,
        aac_capabilities, sizeof(aac_capabilities), aac_configuration, sizeof(aac_configuration));
    if (!aac_endpoint) { printf("[A2DP] Cannot allocate AAC endpoint\n"); exit(3); }
    aac_seid = avdtp_local_seid(aac_endpoint);
    avdtp_stream_endpoint_t* endpoint = a2dp_sink_create_stream_endpoint(AVDTP_AUDIO, AVDTP_CODEC_SBC,
        capabilities, sizeof(capabilities), configuration, sizeof(configuration));
    if (!endpoint) { printf("[A2DP] Cannot allocate endpoint\n"); exit(3); }
    local_seid = avdtp_local_seid(endpoint);
    a2dp_sink_create_sdp_record(sdp_media, sdp_create_service_record_handle(), AVDTP_SINK_FEATURE_MASK_HEADPHONE, "AX201 Stereo Audio", NULL);
    register_record(sdp_media, sizeof(sdp_media));
    avrcp_init(); avrcp_controller_init(); avrcp_target_init();
    avrcp_register_packet_handler(remote); avrcp_controller_register_packet_handler(remote); avrcp_target_register_packet_handler(remote);
    avrcp_controller_create_sdp_record(sdp_control, sdp_create_service_record_handle(),
        1 << AVRCP_CONTROLLER_SUPPORTED_FEATURE_CATEGORY_PLAYER_OR_RECORDER, NULL, NULL);
    register_record(sdp_control, sizeof(sdp_control));
    avrcp_target_create_sdp_record(sdp_target, sdp_create_service_record_handle(),
        1 << AVRCP_TARGET_SUPPORTED_FEATURE_CATEGORY_MONITOR_OR_AMPLIFIER, NULL, NULL);
    register_record(sdp_target, sizeof(sdp_target));
    printf("[A2DP] Audio Sink registered uuid=0x110B; AAC + SBC stereo + AVRCP\n");
}
