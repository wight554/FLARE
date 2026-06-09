# FLARE MMU Sensors Mock Klipper Extra Module
# Enables native mmu_sensors status queries in Fluidd/Mainsail.

class MockFilamentSensor:
    def __init__(self, printer, name, sensor_type):
        self.printer = printer
        self.name = name
        self.sensor_type = sensor_type

    def get_status(self, eventtime):
        mmu = self.printer.lookup_object('mmu', None)
        if mmu is None:
            return {'filament_detected': False, 'enabled': True}
            
        # Get sensor states from mmu object
        pre_gate_0 = bool(mmu.gate_sensor[0]) if len(mmu.gate_sensor) > 0 else False
        pre_gate_1 = bool(mmu.gate_sensor[1]) if len(mmu.gate_sensor) > 1 else False
        gate = bool(mmu.gate_sensor_active)
        extruder = bool(mmu.toolhead_sensor)
        toolhead = bool(mmu.toolhead_sensor)
        hub = bool(mmu.hub_sensor_active)

        # Unify blanking with mmu.py path-cascade model (gear-clear anchored)
        bypass = getattr(mmu, 'bypass', False)
        if bypass:
            pre_gate_0 = False
            pre_gate_1 = False
            gate = False
        elif not gate:
            hub = False
            extruder = False
            toolhead = False

        detected = False
        if 'pre_gate_0' in self.sensor_type:
            detected = pre_gate_0
        elif 'pre_gate_1' in self.sensor_type:
            detected = pre_gate_1
        elif 'gate' in self.sensor_type:
            detected = gate
        elif 'extruder' in self.sensor_type:
            detected = extruder
        elif 'toolhead' in self.sensor_type:
            detected = toolhead
        elif 'hub' in self.sensor_type:
            detected = hub

        return {
            'filament_detected': detected,
            'enabled': True
        }

class MMUSensorsMock:
    def __init__(self, config):
        self.printer = config.get_printer()
        self.name = config.get_name()
        
        # Consume config options to prevent Klipper validation error on unread options
        config.get('pre_gate_switch_pin_0', 'dummy')
        config.get('pre_gate_switch_pin_1', 'dummy')
        config.get('gate_switch_pin', 'dummy')
        config.get('extruder_switch_pin', 'dummy')
        config.get('toolhead_switch_pin', 'dummy')
        
        # Register mmu_sensors mock object
        self.printer.add_object('mmu_sensors', self)

        # Register standard Klipper filament switch sensor mock objects
        # to ensure Fluidd/Mainsail dynamically discover them on the track.
        sensors_to_register = {
            'mmu_pre_gate_0': 'pre_gate_0',
            'mmu_pre_gate_1': 'pre_gate_1',
            'mmu_gate': 'gate',
            'mmu_extruder': 'extruder',
            'mmu_toolhead': 'toolhead',
            'mmu_hub': 'hub',
        }

        for name, sensor_type in sensors_to_register.items():
            object_name = 'filament_switch_sensor ' + name
            if self.printer.lookup_object(object_name, None) is None:
                self.printer.add_object(object_name, MockFilamentSensor(self.printer, name, sensor_type))

    def get_status(self, eventtime):
        mmu = self.printer.lookup_object('mmu', None)
        if mmu is None:
            return {}
            
        # Get sensor states from mmu object
        pre_gate_0 = bool(mmu.gate_sensor[0]) if len(mmu.gate_sensor) > 0 else False
        pre_gate_1 = bool(mmu.gate_sensor[1]) if len(mmu.gate_sensor) > 1 else False
        gate = bool(mmu.gate_sensor_active)
        extruder = bool(mmu.toolhead_sensor)
        toolhead = bool(mmu.toolhead_sensor)
        hub = bool(mmu.hub_sensor_active)

        # Unify blanking with mmu.py path-cascade model (gear-clear anchored)
        bypass = getattr(mmu, 'bypass', False)
        if bypass:
            pre_gate_0 = False
            pre_gate_1 = False
            gate = False
        elif not gate:
            hub = False
            extruder = False
            toolhead = False

        tension = mmu.sync_feedback_state == "tension"
        compression = mmu.sync_feedback_state == "compressed"

        return {
            'pre_gate_0': pre_gate_0,
            'pre_gate_1': pre_gate_1,
            'pre_gate_switch_pin_0': pre_gate_0,
            'pre_gate_switch_pin_1': pre_gate_1,
            'gate': gate,
            'gate_switch_pin': gate,
            'extruder': extruder,
            'extruder_switch_pin': extruder,
            'toolhead': toolhead,
            'toolhead_switch_pin': toolhead,
            'hub': hub,
            'sync_feedback_tension': tension,
            'sync_feedback_compression': compression,
        }

def load_config(config):
    return MMUSensorsMock(config)
