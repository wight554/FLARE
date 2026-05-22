# FLARE MMU Mock Klipper Extra Module
# Enables native MMU panel support in Mainsail/Fluidd without full Happy Hare installation.

class MMUMock:
    def __init__(self, config):
        self.printer = config.get_printer()
        self.name = config.get_name()
        
        # State variables matching Happy Hare structure
        self.enabled = True
        self.num_gates = 2
        self.active_gate = -1
        self.tool = -1
        self.gate_status = [0, 0] # 0 = empty, 1 = loaded/available
        self.gate_sensor = [0, 0] # pre-gate sensor states (filament detected)
        self.toolhead_sensor = 0 # toolhead sensor state
        self.sync_feedback = 0.0 # buffer compression/tension offset
        self.sync_feedback_state = "neutral"
        self.print_job_state = "standby"
        
        # FLARE specific extra state variables
        self.board_online = 0
        self.sps = 0.0
        self.reload_mode = 0

        # Register command to update status
        self.gcode = self.printer.lookup_object('gcode')
        self.gcode.register_command('SET_MMU', self.cmd_SET_MMU,
                                    desc="Update MMU status parameters")

    def cmd_SET_MMU(self, gcmd):
        """Update MMU state parameters dynamically."""
        enabled = gcmd.get_int('ENABLED', None)
        if enabled is not None:
            self.enabled = bool(enabled)

        self.num_gates = gcmd.get_int('NUM_GATES', self.num_gates)
        self.active_gate = gcmd.get_int('ACTIVE_GATE', self.active_gate)
        self.tool = gcmd.get_int('TOOL', self.tool)
        self.toolhead_sensor = gcmd.get_int('TOOLHEAD_SENSOR', self.toolhead_sensor)
        self.sync_feedback = gcmd.get_float('SYNC_FEEDBACK', self.sync_feedback)
        self.sync_feedback_state = gcmd.get('SYNC_FEEDBACK_STATE', self.sync_feedback_state)
        self.print_job_state = gcmd.get('PRINT_JOB_STATE', self.print_job_state)
        
        # FLARE extras
        self.board_online = gcmd.get_int('BOARD_ONLINE', self.board_online)
        self.sps = gcmd.get_float('SPS', self.sps)
        self.reload_mode = gcmd.get_int('RELOAD_MODE', self.reload_mode)

        # Parse gate_status list
        gate_status_str = gcmd.get('GATE_STATUS', None)
        if gate_status_str is not None:
            try:
                self.gate_status = [int(x) for x in gate_status_str.split(',')]
            except ValueError:
                pass

        # Parse gate_sensor list
        gate_sensor_str = gcmd.get('GATE_SENSOR', None)
        if gate_sensor_str is not None:
            try:
                self.gate_sensor = [int(x) for x in gate_sensor_str.split(',')]
            except ValueError:
                pass

    def get_status(self, eventtime):
        """Export state values back to Klipper & Moonraker."""
        return {
            'enabled': self.enabled,
            'num_gates': self.num_gates,
            'active_gate': self.active_gate,
            'tool': self.tool,
            'gate_status': self.gate_status,
            'gate_sensor': self.gate_sensor,
            'toolhead_sensor': self.toolhead_sensor,
            'sync_feedback': self.sync_feedback,
            'sync_feedback_state': self.sync_feedback_state,
            'print_job_state': self.print_job_state,
            'board_online': self.board_online,
            'sps': self.sps,
            'reload_mode': self.reload_mode
        }

def load_config(config):
    return MMUMock(config)
