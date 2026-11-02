# SPDX-License-Identifier: GPL-3.0-only
"""Ableton Control Surface for the M-VAVE FM-1 running the Araminta firmware (3.7 or later).

Install: copy this folder into Live's "MIDI Remote Scripts" directory, restart Live, then pick
"FM-1" as a Control Surface with Input = FM-1 (Preferences > Link, Tempo & MIDI). The FM-1's USB
device is vendor 0x1209, product 0x0001 ("FM-1").
"""
from __future__ import absolute_import, print_function, unicode_literals

from ableton.v2.control_surface.capabilities import (
    CONTROLLER_ID_KEY, NOTES_CC, PORTS_KEY, REMOTE, SCRIPT, controller_id, inport, outport)

from .fm1 import FM1


def get_capabilities():
    return {
        CONTROLLER_ID_KEY: controller_id(vendor_id=0x1209, product_ids=[0x0001], model_name=['FM-1']),
        PORTS_KEY: [
            inport(props=[NOTES_CC, REMOTE, SCRIPT]),
            outport(props=[SCRIPT]),
        ],
    }


def create_instance(c_instance):
    return FM1(c_instance=c_instance)
