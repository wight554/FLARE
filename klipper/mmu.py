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
        self.gate_status = [0, 0] # 0 = empty, 1 = loaded/available, 2 = buffer
        self.gate_sensor = [0, 0] # pre-gate sensor states (filament detected)
        self.gate_color = ["", ""] # color names or hex codes (e.g. "ff0000")
        self.gate_material = ["", ""] # material types (e.g. "PLA", "ABS")
        self.gate_spool_id = [-1, -1] # Spoolman database spool mappings
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
        
        # Register standard Happy Hare G-code commands expected by Mainsail/Fluidd
        self.gcode.register_command('MMU_STATS', self.cmd_MMU_STATS,
                                    desc="Show MMU usage and reliability statistics")
        self.gcode.register_command('MMU_PRELOAD', self.cmd_MMU_PRELOAD,
                                    desc="Preload filament into selected gate")
        self.gcode.register_command('MMU_UNLOAD', self.cmd_MMU_UNLOAD,
                                    desc="Unload filament from extruder to gate")
        self.gcode.register_command('MMU_LOAD', self.cmd_MMU_LOAD,
                                    desc="Load filament from gate to extruder")
        self.gcode.register_command('MMU_EJECT', self.cmd_MMU_EJECT,
                                    desc="Fully eject filament from gate")
        self.gcode.register_command('MMU_RECOVER', self.cmd_MMU_RECOVER,
                                    desc="Recover MMU from error state")
        self.gcode.register_command('MMU_CHECK_GATE', self.cmd_MMU_CHECK_GATE,
                                    desc="Check presence of filament at gate")

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

        # Parse gate_color list
        gate_color_str = gcmd.get('GATE_COLOR', None)
        if gate_color_str is not None:
            self.gate_color = gate_color_str.split(',')

        # Parse gate_material list
        gate_material_str = gcmd.get('GATE_MATERIAL', None)
        if gate_material_str is not None:
            self.gate_material = gate_material_str.split(',')

        # Parse gate_spool_id list
        gate_spool_id_str = gcmd.get('GATE_SPOOL_ID', None)
        if gate_spool_id_str is not None:
            try:
                self.gate_spool_id = [int(x) for x in gate_spool_id_str.split(',')]
            except ValueError:
                pass

    def cmd_MMU_STATS(self, gcmd):
        """Report mock statistics to satisfy Mainsail/Fluidd dashboard status calls."""
        gcmd.respond_info(
            "================ MMU Statistics (FLARE Mock) ================\n"
            "Tool swaps total: 0\n"
            "Swaps successful: 0\n"
            "Swaps failed:     0\n"
            "Success rate:     100.0%\n"
            "Preload successes:0\n"
            "Unload successes: 0\n"
            "Current error:    None"
        )

    def cmd_MMU_PRELOAD(self, gcmd):
        """Map Happy Hare preload to FLARE_LOAD command."""
        gate = gcmd.get_int('GATE', 0)
        lane = gate + 1
        gcmd.respond_info(f"FLARE: Preloading lane {lane} (Gate {gate})")
        self.gcode.run_script_from_command(f"FLARE_LOAD LANE={lane}")

    def cmd_MMU_UNLOAD(self, gcmd):
        """Map Happy Hare unload to FLARE_UNLOAD command."""
        gcmd.respond_info("FLARE: Unloading toolhead and lane")
        self.gcode.run_script_from_command("FLARE_UNLOAD")

    def cmd_MMU_LOAD(self, gcmd):
        """Map Happy Hare load to FLARE_LOAD command."""
        gate = gcmd.get_int('GATE', self.active_gate)
        if gate < 0:
            gate = 0
        lane = gate + 1
        gcmd.respond_info(f"FLARE: Loading lane {lane} (Gate {gate})")
        self.gcode.run_script_from_command(f"FLARE_LOAD LANE={lane}")

    def cmd_MMU_EJECT(self, gcmd):
        """Map Happy Hare eject to FLARE_UNLOAD command."""
        gcmd.respond_info("FLARE: Ejecting filament")
        self.gcode.run_script_from_command("FLARE_UNLOAD")

    def cmd_MMU_RECOVER(self, gcmd):
        """Acknowledge MMU recovery command and log status."""
        gcmd.respond_info("FLARE: Resetting error state and recovering")

    def cmd_MMU_CHECK_GATE(self, gcmd):
        """Acknowledge MMU gate check command."""
        gcmd.respond_info("FLARE: Checking gate status")

    def get_status(self, eventtime):
        """Export state values back to Klipper & Moonraker."""
        return {
            'enabled': self.enabled,
            'num_gates': self.num_gates,
            'active_gate': self.active_gate,
            'tool': self.tool,
            'gate_status': self.gate_status,
            'gate_sensor': self.gate_sensor,
            'gate_color': self.gate_color,
            'gate_material': self.gate_material,
            'gate_spool_id': self.gate_spool_id,
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
