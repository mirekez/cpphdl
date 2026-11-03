# Preserve all devices and connections while aligning physical hierarchy boundaries.
  # Rename local port/net labels only; never merge nets or change pin IDs.
  [["RM_IHPSG13_1024X16_C2_2P_BITKIT_16X2_SRAM", [["VDD_CORE", "VDD!"], ["VSS", "VSS!"]]], ["RM_IHPSG13_1024X16_C2_2P_BITKIT_16X2_EDGE_LR", [["VDD_CORE", "VDD"]]], ["RM_IHPSG13_1024X16_C2_2P_ROWDEC8", [["VDD", "VDD!"], ["VSS", "VSS!"]]], ["RM_IHPSG13_1024X16_C2_2P_ROWREG8", [["VDD", "VDD!"], ["VSS", "VSS!"]]], ["RM_IHPSG13_1024X16_C2_2P_DEC00", [["VDD", "VDD!"], ["VSS", "VSS!"]]], ["RM_IHPSG13_1024X16_C2_2P_DEC01", [["VDD", "VDD!"], ["VSS", "VSS!"]]], ["RM_IHPSG13_1024X16_C2_2P_DEC02", [["VDD", "VDD!"], ["VSS", "VSS!"]]], ["RM_IHPSG13_1024X16_C2_2P_DEC03", [["VDD", "VDD!"], ["VSS", "VSS!"]]], ["RM_IHPSG13_1024X16_C2_2P_DEC04", [["VDD", "VDD!"], ["VSS", "VSS!"]]], ["RM_IHPSG13_1024X16_C2_2P_DLY_MUX", [["VDD", "VDD!"], ["VSS", "VSS!"]]]].each do |name, pairs|
    circuit = schematic.circuit_by_name(name)
    raise "missing named circuit #{name}" unless circuit
    pairs.each do |from, to|
      pin = circuit.pin_by_name(from)
      raise "missing supply port #{name}:#{from}" unless pin
      net = circuit.net_for_pin(pin)
      raise 'supply rename collision' if circuit.pin_by_name(to) || circuit.net_by_name(to)
      raise 'supply port lacks its own local net' unless net && net.name.upcase == from.upcase
      endpoint_signature = lambda do |n|
        [n.each_pin.map(&:pin_id).sort,
         n.each_terminal.map { |t| [t.device.id, t.terminal_id] }.sort,
         n.each_subcircuit_pin.map { |p| [p.subcircuit.id, p.pin_id] }.sort]
      end
      before_binding = endpoint_signature.call(net)
      id = pin.id
      net.name = to
      circuit.rename_pin(id, to)
      raise 'supply rename changed binding' unless endpoint_signature.call(circuit.net_for_pin(circuit.pin_by_name(to))) == before_binding && circuit.pin_by_name(to).id == id
    end
  end
  logger.info('CPPHDL: canonicalized local supply names without changing pin IDs or net bindings')
  # Preserve complete array tiles and matched logic leaves; expand shared-diffusion boundaries.
  keep_circuits = ["RM_IHPSG13_1024X16_C2_2P_BITKIT_16X2_EDGE_LR", "RM_IHPSG13_1024X16_C2_2P_BITKIT_16X2_SRAM", "RM_IHPSG13_1024X16_C2_2P_COLDRV13_FILL4", "RM_IHPSG13_1024X16_C2_2P_COLDRV13_FILL4C2", "RM_IHPSG13_1024X16_C2_2P_DEC00", "RM_IHPSG13_1024X16_C2_2P_DEC01", "RM_IHPSG13_1024X16_C2_2P_DEC02", "RM_IHPSG13_1024X16_C2_2P_DEC03", "RM_IHPSG13_1024X16_C2_2P_DEC04", "RM_IHPSG13_1024X16_C2_2P_DLY_MUX", "RM_IHPSG13_1024X16_C2_2P_ROWDEC8", "RM_IHPSG13_1024X16_C2_2P_ROWREG8", "RM_IHPSG13_2P_1024X16_C2_BM_BIST", "RSC_IHPSG13_AND2X2", "RSC_IHPSG13_CBUFX2", "RSC_IHPSG13_CBUFX4", "RSC_IHPSG13_CBUFX8", "RSC_IHPSG13_CDLYX1", "RSC_IHPSG13_CDLYX1_DUMMY", "RSC_IHPSG13_CDLYX2", "RSC_IHPSG13_CGATEPX4", "RSC_IHPSG13_CINVX4", "RSC_IHPSG13_CINVX4_WN", "RSC_IHPSG13_CNAND2X2", "RSC_IHPSG13_DFNQX2", "RSC_IHPSG13_INVX2", "RSC_IHPSG13_INVX4", "RSC_IHPSG13_LHPQX2", "RSC_IHPSG13_MX2IX1", "RSC_IHPSG13_MX2X2", "RSC_IHPSG13_NAND2X2", "RSC_IHPSG13_NAND3X2", "RSC_IHPSG13_NOR2X2", "RSC_IHPSG13_NOR3X2", "RSC_IHPSG13_OA12X1", "RSC_IHPSG13_TIEH", "RSC_IHPSG13_TIEL", "RSC_IHPSG13_WLDRVX4", "RSC_IHPSG13_XOR2X2"]
  count_devices = lambda do |circuit, memo|
    memo[circuit.name] ||= circuit.each_device.to_a.size + circuit.each_subcircuit.to_a.sum { |s| count_devices.call(s.circuit_ref, memo) }
  end
  [netlist, schematic].each do |nl|
    root = nl.each_circuit.find { |c| c.name.upcase == "RM_IHPSG13_2P_1024X16_C2_BM_BIST" }
    raise 'missing full SRAM top' unless root
    before_count = count_devices.call(root, {})
    flatten_names = nl.each_circuit.map(&:name).reject { |name| keep_circuits.include?(name.upcase) }
    flatten_names.each { |name| nl.flatten_circuit(name) }
    after_count = count_devices.call(root, {})
    raise 'hierarchy flattening lost devices' unless before_count == after_count
    logger.info("CPPHDL: hierarchy expansion preserves #{after_count} instantiated devices")
  end
  # This inverter is already expanded by physical extraction at this boundary.
  a = netlist.circuit_by_name('RM_IHPSG13_1024X16_C2_2P_DEC04')
  b = schematic.circuit_by_name('RM_IHPSG13_1024X16_C2_2P_DEC04')
  raise 'missing decoder boundary' unless a && b
  raise 'physical inverter boundary unexpectedly present' if a.each_subcircuit.any? { |s| s.circuit_ref.name.upcase == 'RSC_IHPSG13_CINVX4' }
  children = b.each_subcircuit.select { |s| s.circuit_ref.name.upcase == 'RSC_IHPSG13_CINVX4' }
  raise 'unexpected reference inverter instance count' unless children.size == 1
  before_count = count_devices.call(b, {})
  children.each { |child| b.flatten_subcircuit(child) }
  raise 'instance expansion lost devices' unless count_devices.call(b, {}) == before_count
  logger.info('CPPHDL: expanded one DEC04 CINVX4 reference instance; all devices preserved')
