# SPDX-License-Identifier: GPL-3.0-only
"""FM-1 as an Ableton Control Surface: the selected device's parameters, sixteen at a time.

KNOB 1-4 of each FM-1 track is one page of four parameters of the device selected on the selected
track: track 1 is parameters 1-4, track 2 5-8, track 3 9-12, track 4 13-16. ALGORITHM selects the FM-1
track, so it selects the page. On a Rack the two banks are its sixteen macros; on any other device they
are the parameters Configure shows.

The parameters come from the same banks the blue hand uses (ableton's _Generic.Devices.parameter_banks),
bound directly, so the FM-1's track bank is the only page switch. The CC map is the v3.7 firmware's:
track n sends CC 16 + 4n .. 19 + 4n on MIDI channel n (ROUT CH1-4, the default).
"""
from __future__ import absolute_import, print_function, unicode_literals

import Live
from _Framework.ControlSurface import ControlSurface
from _Framework.EncoderElement import EncoderElement
from _Framework.InputControlElement import MIDI_CC_TYPE
from _Generic.Devices import parameter_banks

# the FM-1 sends 64 + detents (bin offset); "smooth" is the mode that reads the value as a delta
RELATIVE = Live.MidiMap.MapMode.relative_smooth_binary_offset
CHANNEL_BASE = 0                     # ROUT CH1-4: FM-1 track n -> MIDI channel n. CH5-8 would be 4


def _encoder(channel, cc, name):
    return EncoderElement(MIDI_CC_TYPE, channel, cc, RELATIVE, name=name)


class FM1(ControlSurface):
    """Sixteen encoders, four per FM-1 track, on the selected track's selected device."""

    def __init__(self, *a, **k):
        super(FM1, self).__init__(*a, **k)
        with self.component_guard():
            self._encoders = [
                _encoder(CHANNEL_BASE + t, 16 + 4 * t + k, 'Knob_%d_%d' % (t + 1, k + 1))
                for t in range(4) for k in range(4)
            ]
            self._track = None
            self._device = None
            self.song().view.add_selected_track_listener(self._on_selected_track)
            self._on_selected_track()

    def disconnect(self):
        try:
            self.song().view.remove_selected_track_listener(self._on_selected_track)
            if self._track is not None:
                self._track.view.remove_selected_device_listener(self._on_selected_device)
        except Exception:
            pass
        super(FM1, self).disconnect()

    def _on_selected_track(self):
        track = self.song().view.selected_track
        if self._track is not None:
            self._track.view.remove_selected_device_listener(self._on_selected_device)
        self._track = track
        track.view.add_selected_device_listener(self._on_selected_device)
        self._on_selected_device()

    def _on_selected_device(self):
        device = self._track.view.selected_device if self._track is not None else None
        if device == self._device:
            return
        self._device = device
        params = []
        if device is not None:
            for bank in parameter_banks(device)[:2]:        # two banks of eight: a Rack's sixteen macros
                params.extend(bank)
        for i, encoder in enumerate(self._encoders):
            parameter = params[i] if i < len(params) else None
            if parameter is not None:
                encoder.connect_to(parameter)
            else:
                encoder.release_parameter()
