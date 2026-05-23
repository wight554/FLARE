# FLARE MMU Mock Klipper Extra Module
# Enables native MMU panel support in Mainsail/Fluidd without full Happy Hare installation.

class MMUMachineMock:
    def __init__(self, mmu):
        self.mmu = mmu

    def get_status(self, eventtime):
        return {
            'num_units': 1,
            'unit_0': {
                'name': 'FLARE',
                'vendor': 'FYSETC',
                'version': '1.0',
                'num_gates': self.mmu.num_gates,
                'first_gate': 0,
                'selector_type': 'VirtualSelector',
                'variable_rotation_distances': True,
                'variable_bowden_lengths': True,
                'require_bowden_move': True,
                'filament_always_gripped': False,
                'can_crossload': False,
                'has_bypass': False,
                'multi_gear': False,
                'environment_sensor': '',
                'filament_heater': ''
            }
        }

class MMUMock:
    def __init__(self, config):
        self.printer = config.get_printer()
        self.name = config.get_name()
        
        # Register the mmu_machine mock object to support Multi-Gate spool/gate visibility in Fluidd
        self.printer.add_object('mmu_machine', MMUMachineMock(self))
        
        # State variables matching Happy Hare structure
        self.enabled = True
        self.is_homed = True
        self.num_gates = 2
        self.active_gate = -1
        self.gate = -1
        self.tool = -1
        self.gate_status = [0, 0] # 0 = empty, 1 = loaded/available, 2 = buffer
        self.gate_sensor = [0, 0] # pre-gate sensor states (filament detected)
        self.gate_color = ["", ""] # color names or hex codes (e.g. "ff0000")
        self.gate_material = ["", ""] # material types (e.g. "PLA", "ABS")
        self.gate_spool_id = [-1, -1] # Spoolman database spool mappings
        self.spoolman_support = "get"
        self.gate_color_rgb = [[0.5, 0.5, 0.5], [0.5, 0.5, 0.5]]
        self.gate_name = ["Gate 0", "Gate 1"]
        self.gate_filament_name = ["Gate 0", "Gate 1"]
        self.ttg_map = [0, 1]
        self.action = "Idle"
        self.toolhead_sensor = 0 # toolhead sensor state
        self.sync_feedback = 0.0 # buffer compression/tension offset
        self.sync_feedback_state = "neutral"
        self.print_job_state = "standby"
        self.print_state = "ready"
        self.filament = "Unloaded"
        self.filament_pos = 0
        
        # FLARE specific extra state variables
        self.board_online = 0
        self.sps = 0.0
        self.reload_mode = 0
        self.enable_cutter = 0
        self.unload_cut = 0
        self.gate_sensor_active = 0
        self.extruder_sensor_active = 0
        self.pre_gate_sensor_active = 0
        self.hub_sensor_active = 0

        # Usage statistics, pushed as absolute totals by the daemon (which counts
        # them from board events and persists them). Mirrored here for MMU_STATS
        # and num_toolchanges; not accumulated locally to stay idempotent.
        self.swaps_total = 0
        self.swaps_success = 0
        self.swaps_failed = 0
        self.loads_success = 0
        self.unloads_success = 0
        self.last_error = "None"

        # Register command to update status
        self.gcode = self.printer.lookup_object('gcode')
        self._load_vars()
        self._ensure_array_lengths()
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
        self.gcode.register_command('MMU_CHECK_GATES', self.cmd_MMU_CHECK_GATE,
                                    desc="Check presence of filament at all gates")
        self.gcode.register_command('MMU_GATE_MAP', self.cmd_MMU_GATE_MAP,
                                    desc="Update or list gate filament map")
        self.gcode.register_command('MMU_TTG_MAP', self.cmd_MMU_TTG_MAP,
                                    desc="Update or list tool-to-gate map")
        self.gcode.register_command('MMU_SPOOLMAN', self.cmd_MMU_SPOOLMAN,
                                    desc="Mock Spoolman mapping command")
        self.gcode.register_command('MMU_SELECT', self.cmd_MMU_SELECT,
                                    desc="Select MMU gate or tool")
        self.gcode.register_command('FLARE_WAIT_TC', self.cmd_FLARE_WAIT_TC,
                                    desc="Wait for toolchange physical completion")

        # Graceful handlers for Fluidd maintenance-dialog buttons that cannot be
        # disabled from the mock (gated only by canSend). Map to a real FLARE
        # command where one exists, otherwise no-op so they never throw
        # "Unknown command".
        self.gcode.register_command('MMU_SYNC_GEAR_MOTOR', self.cmd_MMU_SYNC_GEAR_MOTOR,
                                    desc="Enable/disable extruder sync (maps to FLARE SM:)")
        self.gcode.register_command('MMU_MOTORS_ON', self.cmd_MMU_MOTORS_NOOP,
                                    desc="No-op: FLARE drivers are always enabled")
        self.gcode.register_command('MMU_MOTORS_OFF', self.cmd_MMU_MOTORS_NOOP,
                                    desc="No-op: FLARE drivers are always enabled")

    def cmd_SET_MMU(self, gcmd):
        """Update MMU state parameters dynamically."""
        enabled = gcmd.get_int('ENABLED', None)
        if enabled is not None:
            self.enabled = bool(enabled)

        is_homed = gcmd.get_int('IS_HOMED', None)
        if is_homed is not None:
            self.is_homed = bool(is_homed)

        self.num_gates = gcmd.get_int('NUM_GATES', self.num_gates)
        self.active_gate = gcmd.get_int('ACTIVE_GATE', self.active_gate)
        self.gate = gcmd.get_int('GATE', self.gate)
        self.tool = gcmd.get_int('TOOL', self.tool)

        self.toolhead_sensor = gcmd.get_int('TOOLHEAD_SENSOR', self.toolhead_sensor)
        self.sync_feedback = gcmd.get_float('SYNC_FEEDBACK', self.sync_feedback)
        
        # Strip quotes from standard string parameters
        self.sync_feedback_state = gcmd.get('SYNC_FEEDBACK_STATE', self.sync_feedback_state).strip("'\"")
        self.print_job_state = gcmd.get('PRINT_JOB_STATE', self.print_job_state).strip("'\"")
        self.print_state = gcmd.get('PRINT_STATE', self.print_state).strip("'\"")
        self.action = gcmd.get('ACTION', self.action).strip("'\"")
        
        self.board_online = gcmd.get_int('BOARD_ONLINE', self.board_online)
        self.sps = gcmd.get_float('SPS', self.sps)
        self.reload_mode = gcmd.get_int('RELOAD_MODE', self.reload_mode)
        self.enable_cutter = gcmd.get_int('ENABLE_CUTTER', self.enable_cutter)
        self.unload_cut = gcmd.get_int('UNLOAD_CUT', self.unload_cut)
        self.gate_sensor_active = gcmd.get_int('GATE_SENSOR_ACTIVE', self.gate_sensor_active)
        self.extruder_sensor_active = gcmd.get_int('EXTRUDER_SENSOR_ACTIVE', self.extruder_sensor_active)
        self.pre_gate_sensor_active = gcmd.get_int('PRE_GATE_SENSOR_ACTIVE', self.pre_gate_sensor_active)
        self.hub_sensor_active = gcmd.get_int('HUB_SENSOR_ACTIVE', self.hub_sensor_active)
        self.spoolman_support = gcmd.get('SPOOLMAN_SUPPORT', self.spoolman_support).strip("'\"")

        self.swaps_total = gcmd.get_int('SWAPS_TOTAL', self.swaps_total)
        self.swaps_success = gcmd.get_int('SWAPS_SUCCESS', self.swaps_success)
        self.swaps_failed = gcmd.get_int('SWAPS_FAILED', self.swaps_failed)
        self.loads_success = gcmd.get_int('LOADS_SUCCESS', self.loads_success)
        self.unloads_success = gcmd.get_int('UNLOADS_SUCCESS', self.unloads_success)
        self.last_error = gcmd.get('MMU_LAST_ERROR', self.last_error).strip("'\"")
 
        # Parse gate_status list with quote stripping
        gate_status_str = gcmd.get('GATE_STATUS', None)
        if gate_status_str is not None:
            gate_status_str = gate_status_str.strip("'\"")
            try:
                self.gate_status = [int(x.strip("'\" ")) for x in gate_status_str.split(',')]
            except ValueError:
                pass

        # Parse gate_sensor list with quote stripping
        gate_sensor_str = gcmd.get('GATE_SENSOR', None)
        if gate_sensor_str is not None:
            gate_sensor_str = gate_sensor_str.strip("'\"")
            try:
                self.gate_sensor = [int(x.strip("'\" ")) for x in gate_sensor_str.split(',')]
            except ValueError:
                pass

        # Parse gate_color list with quote stripping
        gate_color_str = gcmd.get('GATE_COLOR', None)
        if gate_color_str is not None:
            gate_color_str = gate_color_str.strip("'\"")
            self.gate_color = [x.strip("'\" ") for x in gate_color_str.split(',')]

        # Parse gate_material list with quote stripping
        gate_material_str = gcmd.get('GATE_MATERIAL', None)
        if gate_material_str is not None:
            gate_material_str = gate_material_str.strip("'\"")
            self.gate_material = [x.strip("'\" ") for x in gate_material_str.split(',')]

        # Parse gate_spool_id list with quote stripping
        gate_spool_id_str = gcmd.get('GATE_SPOOL_ID', None)
        if gate_spool_id_str is not None:
            gate_spool_id_str = gate_spool_id_str.strip("'\"")
            try:
                self.gate_spool_id = [int(x.strip("'\" ")) for x in gate_spool_id_str.split(',')]
            except ValueError:
                pass

        # Derive filament loaded state
        loaded_gate = -1
        for g, status in enumerate(self.gate_status):
            if status == 2:
                loaded_gate = g
                break

        is_loaded = False
        if self.toolhead_sensor == 1 and (self.gate == loaded_gate or loaded_gate == -1):
            is_loaded = True
        elif 0 <= self.gate < len(self.gate_status) and self.gate_status[self.gate] == 2:
            is_loaded = True

        if is_loaded:
            self.filament = "Loaded"
            self.filament_pos = 10
        else:
            self.filament = "Unloaded"
            self.filament_pos = 0

        self._ensure_array_lengths()

    def cmd_MMU_STATS(self, gcmd):
        """Report live usage statistics (counted by the daemon from board events)."""
        total = self.swaps_total
        rate = (100.0 * self.swaps_success / total) if total else 100.0
        msg = (
            "================ MMU Statistics (FLARE) ================\n"
            f"Tool swaps total:   {self.swaps_total}\n"
            f"Swaps successful:   {self.swaps_success}\n"
            f"Swaps failed:       {self.swaps_failed}\n"
            f"Success rate:       {rate:.1f}%\n"
            f"Loads successful:   {self.loads_success}\n"
            f"Unloads successful: {self.unloads_success}\n"
            f"Current error:      {self.last_error}"
        )
        if gcmd.get_int('SHOWCOUNTS', 0):
            lines = ["\n--------------- Per-gate -----------------"]
            for i in range(self.num_gates):
                status = self.gate_status[i] if i < len(self.gate_status) else 0
                spool = self.gate_spool_id[i] if i < len(self.gate_spool_id) else -1
                lines.append(f"Gate {i}: status={status} spool_id={spool}")
            msg += "\n".join(lines)
        gcmd.respond_info(msg)

    def cmd_MMU_PRELOAD(self, gcmd):
        """Preload (stage to gate) the selected gate via LO:, not a full load.
        Requires filament at the gate's IN sensor; otherwise LO: would dry-spin
        an empty lane. Note: FLARE auto-preloads on insertion, so this is mainly
        for setups with AUTO_PRELOAD disabled."""
        gate = gcmd.get_int('GATE', self.active_gate)
        if gate < 0:
            gate = 0
        lane = gate + 1
        in_present = gate < len(self.gate_sensor) and self.gate_sensor[gate]
        if not in_present:
            gcmd.respond_info(f"FLARE: No filament at gate {gate} entry sensor; insert filament first")
            return
        gcmd.respond_info(f"FLARE: Preloading lane {lane} (Gate {gate}) to gate")
        self.gcode.run_script_from_command(f"FLARE_PRELOAD LANE={lane}")

    def cmd_MMU_UNLOAD(self, gcmd):
        """Map Happy Hare unload to FLARE_UNLOAD_TOOLHEAD and FLARE_UNLOAD.
        EXTRUDER_ONLY=1 runs only the extruder portion (tip forming + gear
        retract) and skips the trailing UL: gate unload."""
        gcmd.respond_info("FLARE: Unloading toolhead gears")
        self.gcode.run_script_from_command("FLARE_UNLOAD_TOOLHEAD")
        if gcmd.get_int('EXTRUDER_ONLY', 0):
            return
        gcmd.respond_info("FLARE: Unloading lane to gate")
        self.gcode.run_script_from_command("FLARE_UNLOAD")

    def cmd_MMU_LOAD(self, gcmd):
        """Map Happy Hare load to selected-gate FLARE_LOAD and hotend handoff.
        EXTRUDER_ONLY=1 runs only the hotend load + purge (no gate movement,
        no leading FL:)."""
        if gcmd.get_int('EXTRUDER_ONLY', 0):
            gcmd.respond_info("FLARE: Loading hotend only (extruder)")
            self.gcode.run_script_from_command("_FLARE_LOAD_HOTEND")
            return

        gate = gcmd.get_int('GATE', self.active_gate)
        if gate < 0:
            gate = 0
        lane = gate + 1

        other_gate = 1 - gate
        if other_gate < len(self.gate_status):
            if self.gate_status[other_gate] == 2:
                raise gcmd.error(f"FLARE: Cannot load gate {gate} - gate {other_gate} is currently loaded.")

        if self.hub_sensor_active and not self.gate_sensor_active:
            raise gcmd.error(f"FLARE: Cannot load gate {gate} - Y-splitter is occupied by another lane.")

        gcmd.respond_info(f"FLARE: Loading lane {lane} (Gate {gate})")
        self.gcode.run_script_from_command(f"FLARE_LOAD LANE={lane}")

        # Wait synchronously for filament to reach the OUT (gate) sensor
        reactor = self.printer.get_reactor()
        start_time = reactor.monotonic()
        timeout = 15.0
        gcmd.respond_info(f"FLARE: Waiting for lane {lane} OUT sensor...")
        while not self.gate_sensor_active:
            if reactor.monotonic() - start_time > timeout:
                raise gcmd.error(f"FLARE Error: Load timed out waiting for filament to reach gate {gate} OUT sensor.")
            reactor.pause(reactor.monotonic() + 0.2)

        gcmd.respond_info(f"FLARE: Loading lane {lane} into hotend")
        self.gcode.run_script_from_command(f"_FLARE_POST_TC_LOAD LANE={lane}")

    def cmd_MMU_EJECT(self, gcmd):
        """Map Happy Hare eject to selected-gate FLARE_EJECT command."""
        gate = gcmd.get_int('GATE', self.active_gate)
        if gate < 0:
            gate = 0
        if gate >= self.num_gates:
            gcmd.respond_info(f"Error: Gate index {gate} exceeds maximum gates ({self.num_gates})")
            return

        lane = gate + 1
        gate_loaded = gate < len(self.gate_status) and self.gate_status[gate] == 2
        if gate_loaded:
            gcmd.respond_info(f"FLARE: Unloading toolhead gears for lane {lane} (Gate {gate})")
            self.gcode.run_script_from_command("FLARE_UNLOAD_TOOLHEAD")
        else:
            gcmd.respond_info(f"FLARE: Gate {gate} not loaded to toolhead; ejecting gate only")

        gcmd.respond_info(f"FLARE: Ejecting lane {lane} (Gate {gate}) completely")
        self.gcode.run_script_from_command(f"FLARE_EJECT LANE={lane}")

    def cmd_MMU_RECOVER(self, gcmd):
        """Report that recovery is not implemented. FLARE has no error-lock state
        to recover from; this message exists because Fluidd's Recover button
        cannot be disabled from the mock."""
        gcmd.respond_raw("!! MMU_RECOVER is not implemented on FLARE (no error-lock state to recover from).")

    def cmd_MMU_SYNC_GEAR_MOTOR(self, gcmd):
        """Map Happy Hare gear-sync toggle to FLARE SM: (extruder sync)."""
        sync = gcmd.get_int('SYNC', 1)
        self.gcode.run_script_from_command(f'RUN_SHELL_COMMAND CMD=flare PARAMS="SM:{1 if sync else 0}"')
        gcmd.respond_info(f"FLARE: Extruder sync {'enabled' if sync else 'disabled'}")

    def cmd_MMU_MOTORS_NOOP(self, gcmd):
        """No-op: FLARE stepper drivers are firmware-managed; no host toggle."""
        gcmd.respond_raw("!! MMU motor on/off is not implemented on FLARE (drivers are firmware-managed).")

    def cmd_MMU_CHECK_GATE(self, gcmd):
        """Acknowledge MMU gate check command and report status."""
        try:
            self.gcode.run_script_from_command('RUN_SHELL_COMMAND CMD=flare PARAMS="?:"')
        except Exception:
            pass
            
        status_names = {0: "Empty", 1: "Preloaded", 2: "Loaded/Buffer"}
        msg = "================ MMU Gate Check (FLARE Mock) ================\n"
        for i in range(self.num_gates):
            status_val = self.gate_status[i] if i < len(self.gate_status) else 0
            status_str = status_names.get(status_val, "Unknown")
            sensor_val = self.gate_sensor[i] if i < len(self.gate_sensor) else 0
            sensor_str = "Detected" if sensor_val else "Not Detected"
            
            material = "Unknown"
            if i < len(self.gate_material) and self.gate_material[i]:
                material = self.gate_material[i]
                
            color = "Unknown"
            if i < len(self.gate_color) and self.gate_color[i]:
                color = self.gate_color[i]
                
            msg += f"Gate {i}: Status: {status_str} | Sensor: {sensor_str} | Material: {material} | Color: {color}\n"
        gcmd.respond_info(msg)

    def cmd_MMU_GATE_MAP(self, gcmd):
        """Update or list gate-to-filament mappings."""
        map_str = gcmd.get('MAP', None)
        if map_str is not None:
            try:
                import ast
                map_str = map_str.strip("'\"")
                parsed_map = ast.literal_eval(map_str)
                if isinstance(parsed_map, dict):
                    for g_key, data in parsed_map.items():
                        try:
                            g_idx = int(g_key)
                        except ValueError:
                            continue
                        if g_idx < 0 or g_idx >= self.num_gates:
                            continue
                        if not isinstance(data, dict):
                            continue
                        
                        # Update gate properties
                        if 'material' in data:
                            self.gate_material[g_idx] = str(data['material'])
                        
                        if 'color' in data:
                            color = str(data['color']).lstrip('#')
                            if len(color) == 8:  # Strip alpha if 8-char (e.g. 000000FF -> 000000)
                                color = color[:6]
                            self.gate_color[g_idx] = color
                            if len(color) == 6:
                                try:
                                    r = int(color[0:2], 16) / 255.0
                                    g = int(color[2:4], 16) / 255.0
                                    b = int(color[4:6], 16) / 255.0
                                    self.gate_color_rgb[g_idx] = [r, g, b]
                                except ValueError:
                                    pass
                        
                        if 'name' in data:
                            name = str(data['name'])
                            self.gate_name[g_idx] = name
                            self.gate_filament_name[g_idx] = name
                        
                        if 'spool_id' in data:
                            try:
                                self.gate_spool_id[g_idx] = int(data['spool_id'])
                            except (ValueError, TypeError):
                                pass
                                
                        if 'status' in data:
                            try:
                                self.gate_status[g_idx] = int(data['status'])
                            except (ValueError, TypeError):
                                pass
                                
                    self._save_vars()
                    self._ensure_array_lengths()
                    gcmd.respond_info("FLARE: Updated MMU gate map from MAP dictionary")
                    return
            except Exception as e:
                gcmd.respond_info(f"FLARE Error: Failed to parse MAP: {e}")

        gate = gcmd.get_int('GATE', -1)
        if gate < 0:
            gcmd.respond_info(
                "================ MMU Gate Map (FLARE Mock) ================\n"
                f"Gate 0: {self.gate_material[0]}({self.gate_color[0]}) Status: {self.gate_status[0]}\n"
                f"Gate 1: {self.gate_material[1]}({self.gate_color[1]}) Status: {self.gate_status[1]}"
            )
            return

        if gate >= self.num_gates:
            gcmd.respond_info(f"Error: Gate index {gate} exceeds maximum gates ({self.num_gates})")
            return

        material = gcmd.get('MATERIAL', None)
        if material is not None:
            self.gate_material[gate] = material

        color = gcmd.get('COLOR', None)
        if color is not None:
            color = color.lstrip('#')
            if len(color) == 8:
                color = color[:6]
            self.gate_color[gate] = color
            if len(color) == 6:
                try:
                    r = int(color[0:2], 16) / 255.0
                    g = int(color[2:4], 16) / 255.0
                    b = int(color[4:6], 16) / 255.0
                    self.gate_color_rgb[gate] = [r, g, b]
                except ValueError:
                    pass

        name = gcmd.get('NAME', None)
        if name is not None:
            self.gate_name[gate] = name
            self.gate_filament_name[gate] = name

        spool_id = gcmd.get_int('SPOOL_ID', None)
        if spool_id is not None:
            self.gate_spool_id[gate] = spool_id

        available = gcmd.get_int('AVAILABLE', None)
        if available is not None:
            self.gate_status[gate] = available

        self._save_vars()
        self._ensure_array_lengths()
        gcmd.respond_info(f"FLARE: Updated Gate {gate} map")


    def cmd_MMU_TTG_MAP(self, gcmd):
        """Update or list tool-to-gate mappings."""
        tool = gcmd.get_int('TOOL', -1)
        gate = gcmd.get_int('GATE', -1)
        
        if tool < 0 or gate < 0:
            gcmd.respond_info(
                "================ MMU Tool-to-Gate Map (FLARE Mock) ================\n"
                f"Tool 0 -> Gate {self.ttg_map[0]}\n"
                f"Tool 1 -> Gate {self.ttg_map[1]}"
            )
            return

        if tool >= len(self.ttg_map) or gate >= self.num_gates:
            gcmd.respond_info(f"Error: Invalid Tool {tool} or Gate {gate}")
            return

        self.ttg_map[tool] = gate
        self._save_vars()
        self._ensure_array_lengths()
        gcmd.respond_info(f"FLARE: Mapped Tool {tool} to Gate {gate}")

    def cmd_MMU_SPOOLMAN(self, gcmd):
        """Mock Spoolman mapping command."""
        gate = gcmd.get_int('GATE', -1)
        if gate < 0:
            # Show current mapping
            gcmd.respond_info(
                "================ MMU Spoolman Map (FLARE Mock) ================\n"
                f"Gate 0: Spool ID {self.gate_spool_id[0]}\n"
                f"Gate 1: Spool ID {self.gate_spool_id[1]}"
            )
            return

        if gate >= self.num_gates:
            gcmd.respond_info(f"Error: Gate index {gate} exceeds maximum gates ({self.num_gates})")
            return

        clear = gcmd.get_int('CLEAR', 0)
        if clear:
            self.gate_spool_id[gate] = -1
            self._save_vars()
            self._ensure_array_lengths()
            gcmd.respond_info(f"FLARE: Cleared Spool ID for Gate {gate}")
            return

        spool_id = gcmd.get_int('SPOOLID', None)
        if spool_id is None:
            spool_id = gcmd.get_int('SPOOL', None)

        if spool_id is not None:
            self.gate_spool_id[gate] = spool_id
            self._save_vars()
            self._ensure_array_lengths()
            gcmd.respond_info(f"FLARE: Mapped Gate {gate} to Spool ID {spool_id}")
        else:
            gcmd.respond_info(f"Gate {gate} currently mapped to Spool ID {self.gate_spool_id[gate]}")

    def cmd_MMU_SELECT(self, gcmd):
        """Map Happy Hare select to FLARE toolchange or pure UI active gate selection."""
        gate = gcmd.get_int('GATE', -1)
        tool = gcmd.get_int('TOOL', -1)
        
        # Check if TOOL was explicitly passed to distinguish between UI gate selection
        # and physical toolchange.
        has_tool = gcmd.get('TOOL', None) is not None
        
        if gate < 0 and tool >= 0:
            if tool < len(self.ttg_map):
                gate = self.ttg_map[tool]
        
        if gate < 0:
            gcmd.respond_info("Error: MMU_SELECT requires either GATE or TOOL parameter")
            return
            
        if gate >= self.num_gates:
            gcmd.respond_info(f"Error: Gate index {gate} exceeds maximum gates ({self.num_gates})")
            return
            
        if gate == self.active_gate and self.gate == gate and self.tool == gate:
            gcmd.respond_info(f"FLARE: Lane {gate + 1} (Gate {gate}) already active.")
            return

        lane = gate + 1
        if has_tool or tool >= 0:
            # Physical toolchange requested
            gcmd.respond_info(f"FLARE: Performing toolchange to lane {lane} (Gate {gate})")
            self.gcode.run_script_from_command(f"_FLARE_CHANGE_LANE LANE={lane}")
        else:
            # Pure UI active gate selection: update active_gate to change button states dynamically
            gcmd.respond_info(f"FLARE: Selecting active gate {gate} (Lane {lane})")
            self.active_gate = gate
            self.gate = gate
            self.tool = gate
            self._ensure_array_lengths()
            try:
                self.gcode.run_script_from_command(f'RUN_SHELL_COMMAND CMD=flare PARAMS="T:{lane}"')
            except Exception as e:
                gcmd.respond_info(f"FLARE: Warning: Failed to send T:{lane} to board: {str(e)}")

    def _is_toolhead_sensor_triggered(self):
        # 1. Try to query the filament switch sensor object in Klipper directly
        try:
            sensor_obj = self.printer.lookup_object('filament_switch_sensor toolhead_sensor')
            eventtime = self.printer.get_reactor().monotonic()
            status = sensor_obj.get_status(eventtime)
            if status.get('filament_detected'):
                return True
        except Exception:
            pass

        # 2. Fall back to the synchronizer-driven cached property
        return self.toolhead_sensor == 1

    def cmd_FLARE_WAIT_TC(self, gcmd):
        """Wait synchronously for physical toolchange toolhead insertion."""
        lane = gcmd.get_int('LANE', 0)
        load_delay = gcmd.get_float('LOAD_DELAY', 2.0)
        timeout = gcmd.get_float('TIMEOUT', 300.0)
        
        gcmd.respond_info(f"FLARE: FLARE_WAIT_TC waiting for toolhead sensor (Lane {lane}, timeout {timeout}s)...")
        
        # Flush existing moves first
        toolhead = self.printer.lookup_object('toolhead')
        toolhead.wait_moves()
        
        reactor = self.printer.get_reactor()
        start_time = reactor.monotonic()
        
        # Poll toolhead sensor status in a non-blocking loop
        while not self._is_toolhead_sensor_triggered():
            if reactor.monotonic() - start_time > timeout:
                raise gcmd.error("FLARE Error: Toolchange timed out.")
            
            reactor.pause(reactor.monotonic() + 0.2)
            
        gcmd.respond_info(f"FLARE: Toolhead sensor triggered. Waiting load_delay of {load_delay}s for stabilization...")
        
        # Additional stabilization delay
        if load_delay > 0:
            reactor.pause(reactor.monotonic() + load_delay)
            
        gcmd.respond_info("FLARE: FLARE_WAIT_TC wait complete.")

    def _get_vars_path(self):
        import os
        # Try standard paths, fallback to /tmp if write-restricted
        paths = [
            os.path.expanduser("~/printer_data/config/flare_mmu_vars.json"),
            os.path.expanduser("~/flare_mmu_vars.json"),
            "/tmp/flare_mmu_vars.json"
        ]
        for p in paths:
            try:
                dir_path = os.path.dirname(p)
                if not os.path.exists(dir_path):
                    continue
                # Test write capability directly by touching
                test_file = os.path.join(dir_path, ".flare_write_test")
                with open(test_file, "w") as f:
                    f.write("test")
                os.remove(test_file)
                return p
            except Exception:
                pass
        return "/tmp/flare_mmu_vars.json"

    def _save_vars(self):
        import json
        try:
            data = {
                "gate_color": self.gate_color,
                "gate_material": self.gate_material,
                "gate_spool_id": self.gate_spool_id,
                "gate_color_rgb": self.gate_color_rgb,
                "gate_name": self.gate_name,
                "gate_filament_name": self.gate_filament_name,
                "ttg_map": self.ttg_map,
                "spoolman_support": self.spoolman_support
            }
            path = self._get_vars_path()
            with open(path, "w") as f:
                json.dump(data, f)
        except Exception as e:
            if hasattr(self, 'gcode'):
                self.gcode.respond_info(f"FLARE Error: Failed to save MMU vars: {e}")

    def _load_vars(self):
        import json
        import os
        try:
            path = self._get_vars_path()
            if os.path.exists(path):
                with open(path, "r") as f:
                    data = json.load(f)
                self.gate_color = data.get("gate_color", self.gate_color)
                self.gate_material = data.get("gate_material", self.gate_material)
                self.gate_spool_id = data.get("gate_spool_id", self.gate_spool_id)
                self.gate_color_rgb = data.get("gate_color_rgb", self.gate_color_rgb)
                self.gate_name = data.get("gate_name", self.gate_name)
                self.gate_filament_name = data.get("gate_filament_name", self.gate_filament_name)
                self.ttg_map = data.get("ttg_map", self.ttg_map)
                self.spoolman_support = data.get("spoolman_support", self.spoolman_support)
        except Exception as e:
            pass
        self._ensure_array_lengths()

    def _ensure_array_lengths(self):
        n = self.num_gates
        
        # Helper to pad or truncate list to exactly n elements
        def pad_list(lst, default_val):
            import copy
            if not isinstance(lst, list):
                lst = []
            while len(lst) < n:
                if callable(default_val):
                    lst.append(default_val(len(lst)))
                else:
                    lst.append(copy.deepcopy(default_val))
            return lst[:n]

        self.gate_status = pad_list(self.gate_status, 0)
        self.gate_sensor = pad_list(self.gate_sensor, 0)
        self.gate_color = pad_list(self.gate_color, "")
        self.gate_material = pad_list(self.gate_material, "")
        self.gate_spool_id = pad_list(self.gate_spool_id, -1)
        self.gate_color_rgb = pad_list(self.gate_color_rgb, [0.5, 0.5, 0.5])
        self.gate_name = pad_list(self.gate_name, lambda i: f"Gate {i}")
        self.gate_filament_name = pad_list(self.gate_filament_name, lambda i: f"Gate {i}")
        self.ttg_map = pad_list(self.ttg_map, lambda i: i)

        # Force fresh list objects to trigger Klipper/Moonraker status updates
        self.gate_status = list(self.gate_status)
        self.gate_sensor = list(self.gate_sensor)
        self.gate_color = list(self.gate_color)
        self.gate_material = list(self.gate_material)
        self.gate_spool_id = list(self.gate_spool_id)
        self.gate_color_rgb = [list(rgb) for rgb in self.gate_color_rgb]
        self.gate_name = list(self.gate_name)
        self.gate_filament_name = list(self.gate_filament_name)
        self.ttg_map = list(self.ttg_map)

    @property
    def tool_color(self):
        return [self.gate_color[g] if 0 <= g < len(self.gate_color) else "" for g in self.ttg_map]

    @property
    def tool_material(self):
        return [self.gate_material[g] if 0 <= g < len(self.gate_material) else "" for g in self.ttg_map]

    @property
    def tool_spool_id(self):
        return [self.gate_spool_id[g] if 0 <= g < len(self.gate_spool_id) else -1 for g in self.ttg_map]

    @property
    def tool_color_rgb(self):
        return [self.gate_color_rgb[g] if 0 <= g < len(self.gate_color_rgb) else [0.5, 0.5, 0.5] for g in self.ttg_map]

    @property
    def tool_name(self):
        return [self.gate_name[g] if 0 <= g < len(self.gate_name) else f"Tool {i}" for i, g in enumerate(self.ttg_map)]

    @property
    def tool_filament_name(self):
        return [self.gate_filament_name[g] if 0 <= g < len(self.gate_filament_name) else f"Tool {i}" for i, g in enumerate(self.ttg_map)]

    def get_status(self, eventtime):
        """Export state values back to Klipper & Moonraker."""
        # Filament-path sensor cascade: a strand cannot be past a sensor it has
        # not reached. Once the per-lane gear (OUT) sensor is clear, the shared
        # downstream sensors (hub/gate, toolhead) cannot belong to this lane, so
        # force them clear. Suppresses shared-sensor leakage when viewing a
        # non-loaded gate. Anchored at gear, not pre-gate, so a spool runout
        # (IN clear, filament still threaded past the drive) is not blanked.
        path_pre_gate = bool(self.pre_gate_sensor_active)
        path_gear = bool(self.gate_sensor_active)
        path_gate = bool(self.hub_sensor_active)
        path_toolhead = bool(self.toolhead_sensor)
        if not path_gear:
            path_gate = False
            path_toolhead = False
        return {
            'enabled': self.enabled,
            'is_homed': self.is_homed,
            'num_gates': self.num_gates,
            'active_gate': self.active_gate,
            'gate': self.gate,
            'tool': self.tool,
            'gate_status': self.gate_status,
            'gate_sensor': self.gate_sensor,
            'gate_color': self.gate_color,
            'gate_material': self.gate_material,
            'gate_spool_id': self.gate_spool_id,
            'gate_color_rgb': self.gate_color_rgb,
            'gate_name': self.gate_name,
            'gate_filament_name': self.gate_filament_name,
            'ttg_map': self.ttg_map,
            'tool_color': self.tool_color,
            'tool_material': self.tool_material,
            'tool_spool_id': self.tool_spool_id,
            'tool_color_rgb': self.tool_color_rgb,
            'tool_name': self.tool_name,
            'tool_filament_name': self.tool_filament_name,
            'action': self.action,
            'num_toolchanges': self.swaps_total,
            'swaps_total': self.swaps_total,
            'swaps_success': self.swaps_success,
            'swaps_failed': self.swaps_failed,
            'loads_success': self.loads_success,
            'unloads_success': self.unloads_success,
            'last_error': self.last_error,
            'toolhead_sensor': self.toolhead_sensor,
            'sync_feedback': self.sync_feedback,
            'sync_feedback_state': self.sync_feedback_state,
            'sync_feedback_bias_modelled': self.sync_feedback,
            'sync_feedback_enabled': True,
            'print_job_state': self.print_job_state,
            'print_state': self.print_state,
            'board_online': self.board_online,
            'sps': self.sps,
            'reload_mode': self.reload_mode,
            'enable_cutter': self.enable_cutter,
            'unload_cut': self.unload_cut,
            'spoolman_support': self.spoolman_support,
            'filament': self.filament,
            'filament_pos': self.filament_pos,
            'gate_sensor_active': self.gate_sensor_active,
            'extruder_sensor_active': self.extruder_sensor_active,
            'pre_gate_sensor_active': self.pre_gate_sensor_active,
            'hub_sensor_active': self.hub_sensor_active,
            'sensors': {
                'mmu_pre_gate': path_pre_gate,
                'mmu_gear': path_gear,
                'mmu_gate': path_gate,
                'toolhead': path_toolhead,
                'filament_tension': self.sync_feedback_state == "tension",
                'filament_compression': self.sync_feedback_state == "compressed",
            }
        }

def load_config(config):
    return MMUMock(config)
