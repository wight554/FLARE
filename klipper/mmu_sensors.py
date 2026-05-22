# FLARE MMU Sensors Mock Klipper Extra Module
# Enables native mmu_sensors status queries in Fluidd/Mainsail.

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
        tension = mmu.sync_feedback_state == "expanded"
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
