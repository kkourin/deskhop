/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 * Copyright (c) 2025 Hrvoje Cavrak
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * See the file LICENSE for the full license text.
 */
#include "hid_report.h"
#include "main.h"

/* Given a value struct with size and offset in bits, find and return a value from the HID report */
int32_t get_report_value(uint8_t *report, int len, report_val_t *val) {
    /* Calculate the bit offset within the byte */
    uint16_t offset_in_bits = val->offset % 8;

    /* Calculate the remaining bits in the first byte */
    uint16_t remaining_bits = 8 - offset_in_bits;

    /* Calculate the byte offset in the array */
    uint16_t byte_offset = val->offset >> 3;

    if (byte_offset >= len)
        return 0;

    /* Create a mask for the specified number of bits */
    uint32_t mask = (1u << val->size) - 1;

    /* Initialize the result value with the bits from the first byte */
    int32_t result = report[byte_offset] >> offset_in_bits;

    /* Move to the next byte and continue fetching bits until the desired length is reached */
    while (val->size > remaining_bits) {
        result |= report[++byte_offset] << remaining_bits;
        remaining_bits += 8;
    }

    /* Apply the mask to retain only the desired number of bits */
    result = result & mask;

    /* Special case if our result is negative.
       Check if the most significant bit of 'val' is set */
    if (result & ((mask >> 1) + 1)) {
        /* If it is set, sign-extend 'val' by filling the higher bits with 1s */
        result |= (0xFFFFFFFFU << val->size);
    }

    return result;
}

/* After processing the descriptor, assign the values so we can later use them to interpret reports */
void handle_consumer_control_values(report_val_t *src, report_val_t *dst, hid_interface_t *iface) {
    if (src->offset > MAX_CC_BUTTONS) {
        return;
    }

    if (src->data_type == VARIABLE) {
        iface->keyboard.cc_array[src->offset] = src->usage;
        iface->consumer.is_variable = true;
    }

    iface->consumer.is_array |= (src->data_type == ARRAY);
}

/* After processing the descriptor, assign the values so we can later use them to interpret reports */
void handle_system_control_values(report_val_t *src, report_val_t *dst, hid_interface_t *iface) {
    if (src->offset > MAX_SYS_BUTTONS) {
        return;
    }

    if (src->data_type == VARIABLE) {
        iface->keyboard.sys_array[src->offset] = src->usage;
        iface->system.is_variable = true;
    }

    iface->system.is_array |= (src->data_type == ARRAY);
}

/* After processing the descriptor, assign the values so we can later use them to interpret reports */
void handle_keyboard_descriptor_values(report_val_t *src, report_val_t *dst, hid_interface_t *iface) {
    const int LEFT_CTRL = 0xE0;

    /* Constants are normally used for padding, so skip'em */
    if (src->item_type == CONSTANT)
        return;

    /* Detect and handle modifier keys. <= if modifier is less + constant padding? */
    if (src->size <= MODIFIER_BIT_LENGTH && src->data_type == VARIABLE) {
        /* To make sure this really is the modifier key, we expect e.g. left control to be
           within the usage interval */
        if (LEFT_CTRL >= src->usage_min && LEFT_CTRL <= src->usage_max)
            iface->keyboard.modifier = *src;
    }

    /* If we have an array member, that's most likely a key (0x00 - 0xFF, 1 byte) */
    if (src->offset_idx < MAX_KEYS) {
        iface->keyboard.key_array[src->offset_idx] = (src->data_type == ARRAY);
    }

    /*
    Wooting dump:

    0x19, 0x04,        //   Usage Minimum (0x04)
    0x29, 0x31,        //   Usage Maximum (0x31)
    0x95, 0x2E,        //   Report Count (46)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

    0x95, 0x02,        //   Report Count (2)
    0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

    0x19, 0x33,        //   Usage Minimum (0x33)
    0x29, 0x9B,        //   Usage Maximum (0x9B)
    0x95, 0x69,        //   Report Count (105)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

    0x95, 0x07,        //   Report Count (7)
    0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

    0x19, 0x9D,        //   Usage Minimum (0x9D)
    0x29, 0xA4,        //   Usage Maximum (0xA4)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

    0x19, 0xB0,        //   Usage Minimum (0xB0)
    0x29, 0xDD,        //   Usage Maximum (0xDD)
    0x95, 0x2E,        //   Report Count (46)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

    0x95, 0x02,        //   Report Count (2)
    0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

    */

    // TODO(colin): handle the size 8 range Usage Minimum (0x9D)-Usage Minimum (0xA4) on wooting
    if (src->size > 32 && src->data_type == VARIABLE) {
        iface->keyboard.is_nkro = true;
        iface->keyboard.nkro    = *src;

        if (iface->keyboard.num_nkro < MAX_NKRO2) {
            iface->keyboard.nkro2[iface->keyboard.num_nkro] = *src;
            ++iface->keyboard.num_nkro;
        }
    }

    /* We found a keyboard on this interface. */
    iface->keyboard.is_found = true;
}

void handle_buttons(report_val_t *src, report_val_t *dst, hid_interface_t *iface) {
    /* Constant is normally used for padding with mouse buttons, aggregate to simplify things */
    if (src->item_type == CONSTANT) {
        iface->mouse.buttons.size += src->size;
        return;
    }

    iface->mouse.buttons = *src;

    /* We found a mouse on this interface. */
    iface->mouse.is_found = true;
}

void _store(report_val_t *src, report_val_t *dst, hid_interface_t *iface) {
    if (src->item_type != CONSTANT)
        *dst = *src;
}


void extract_data(hid_interface_t *iface, report_val_t *val) {
    const usage_map_t map[] = {
        {.usage_page   = HID_USAGE_PAGE_BUTTON,
         .global_usage = HID_USAGE_DESKTOP_MOUSE,
         .handler      = handle_buttons,
         .receiver     = process_mouse_report,
         .dst          = &iface->mouse.buttons,
         .id           = &iface->mouse.report_id},

        {.usage_page   = HID_USAGE_PAGE_DESKTOP,
         .global_usage = HID_USAGE_DESKTOP_MOUSE,
         .usage        = HID_USAGE_DESKTOP_X,
         .handler      = _store,
         .receiver     = process_mouse_report,
         .dst          = &iface->mouse.move_x,
         .id           = &iface->mouse.report_id},

        {.usage_page   = HID_USAGE_PAGE_DESKTOP,
         .global_usage = HID_USAGE_DESKTOP_MOUSE,
         .usage        = HID_USAGE_DESKTOP_Y,
         .handler      = _store,
         .receiver     = process_mouse_report,
         .dst          = &iface->mouse.move_y,
         .id           = &iface->mouse.report_id},

        {.usage_page   = HID_USAGE_PAGE_DESKTOP,
         .global_usage = HID_USAGE_DESKTOP_MOUSE,
         .usage        = HID_USAGE_DESKTOP_WHEEL,
         .handler      = _store,
         .receiver     = process_mouse_report,
         .dst          = &iface->mouse.wheel,
         .id           = &iface->mouse.report_id},

        {.usage_page   = HID_USAGE_PAGE_CONSUMER,
         .global_usage = HID_USAGE_DESKTOP_MOUSE,
         .usage        = HID_USAGE_CONSUMER_AC_PAN,
         .handler      = _store,
         .receiver     = process_mouse_report,
         .dst          = &iface->mouse.pan,
         .id           = &iface->mouse.report_id},

        {.usage_page   = HID_USAGE_PAGE_KEYBOARD,
         .global_usage = HID_USAGE_DESKTOP_KEYBOARD,
         .handler      = handle_keyboard_descriptor_values,
         .receiver     = process_keyboard_report,
         .id           = &iface->keyboard.report_id},

        {.usage_page   = HID_USAGE_PAGE_CONSUMER,
         .global_usage = HID_USAGE_CONSUMER_CONTROL,
         .handler      = handle_consumer_control_values,
         .receiver     = process_consumer_report,
         .dst          = &iface->consumer.val,
         .id           = &iface->consumer.report_id},

        {.usage_page   = HID_USAGE_PAGE_DESKTOP,
         .global_usage = HID_USAGE_DESKTOP_SYSTEM_CONTROL,
         .handler      = _store,
         .receiver     = process_system_report,
         .dst          = &iface->system.val,
         .id           = &iface->system.report_id},
    };

    /* We extracted all we could find in the descriptor to report_values, now go through them and
       match them up with the values in the table above, then store those values for later reference */

    for (const usage_map_t *hay = map; hay != &map[ARRAY_SIZE(map)]; hay++) {
        /* ---> If any condition is not defined, we consider it as matched <--- */
        bool global_usages_match = (val->global_usage == hay->global_usage) || (hay->global_usage == 0);
        bool usages_match        = (val->usage == hay->usage) || (hay->usage == 0);
        bool usage_pages_match   = (val->usage_page == hay->usage_page) || (hay->usage_page == 0);

        if (global_usages_match && usages_match && usage_pages_match) {
            hay->handler(val, hay->dst, iface);
            *hay->id = val->report_id;

            if (val->report_id < MAX_REPORTS)
                iface->report_handler[val->report_id] = hay->receiver;
        }
    }
}

int32_t extract_bit_variable(report_val_t *kbd, uint8_t *raw_report, int len, uint8_t *dst, int key_count) {
    int bit_offset = kbd->offset & 0b111;

    for (int i = kbd->usage_min, j = bit_offset; i <= kbd->usage_max && key_count < len; i++, j++) {
        int byte_index = j >> 3;
        int bit_index  = j & 0b111;

        if (raw_report[byte_index] & (1 << bit_index)) {
            dst[key_count++] = i;
        }
    }

    return key_count;
}

int32_t _extract_kbd_boot(uint8_t *raw_report, int len, hid_keyboard_report_t *report) {
    uint8_t *src = raw_report;

    /* In case keyboard still uses report ID in this, just pick the last 8 bytes */
    if (len == KBD_REPORT_LENGTH + 1)
        src++;

    memcpy(report, src, KBD_REPORT_LENGTH);
    return KBD_REPORT_LENGTH;
}

int32_t _extract_kbd_other(uint8_t *raw_report, int len, hid_interface_t *iface, hid_keyboard_report_t *report) {
    uint8_t *src = raw_report;
    keyboard_t *kb = &iface->keyboard;

    if (iface->uses_report_id)
        src++;

    report->modifier = src[kb->modifier.offset_idx];
    for (int i=0, j=0; i < MAX_KEYS && j < KEYS_IN_USB_REPORT; i++) {
        if(kb->key_array[i])
            report->keycode[j++] = src[i];
    }

    return KBD_REPORT_LENGTH;
}

int32_t _extract_kbd_nkro(uint8_t *raw_report, int len, hid_interface_t *iface, hid_keyboard_report_t *report) {
    uint8_t *ptr = raw_report;
    keyboard_t *kb = &iface->keyboard;

    /* Skip report ID */
    if (iface->uses_report_id)
        ptr++;

    /* We expect array of bits mapping 1:1 from usage_min to usage_max, otherwise panic */
    for (int i = 0; i < kb->num_nkro; ++i) {
        if ((kb->nkro2[i].usage_max - kb->nkro2[i].usage_min + 1) != kb->nkro2[i].size) {
            return -1;
        }
    }

    if ((kb->nkro.usage_max - kb->nkro.usage_min + 1) != kb->nkro.size)
        return -1;

    /* We expect modifier to be 8 bits long, otherwise we'll fallback to boot mode */
    if (kb->modifier.size == MODIFIER_BIT_LENGTH) {
        report->modifier = ptr[kb->modifier.offset_idx];
    } else
        return -1;

    /* Move the pointer to the nkro offset's byte index */
    //ptr = &ptr[kb->nkro.offset_idx];  // Don't use original nkro

    int num_keys = 0;
    for (int i = 0; i < kb->num_nkro; ++i) {
        uint8_t* cur_ptr = &ptr[kb->nkro2[i].offset_idx];
        num_keys = extract_bit_variable(&kb->nkro2[i], cur_ptr, KEYS_IN_USB_REPORT, &(report->keycode[num_keys]), num_keys);
    }

    return num_keys;
}

int32_t extract_kbd_data(
    uint8_t *raw_report, int len, uint8_t itf, hid_interface_t *iface, hid_keyboard_report_t *report) {

    /* Clear the report to start fresh */
    memset(report, 0, KBD_REPORT_LENGTH);

    /* If we're in boot protocol mode, then it's easy to decide. */
    if (iface->protocol == HID_PROTOCOL_BOOT)
        return _extract_kbd_boot(raw_report, len, report);

    /* NKRO is a special case */
    if (iface->keyboard.is_nkro)
        return _extract_kbd_nkro(raw_report, len, iface, report);

    /* If we're getting 8 bytes of report, it's safe to assume standard modifier + reserved + keys */
    if (len == KBD_REPORT_LENGTH || len == KBD_REPORT_LENGTH + 1)
        return _extract_kbd_boot(raw_report, len, report);

    /* This is something completely different, look at the report  */
    return _extract_kbd_other(raw_report, len, iface, report);
}
