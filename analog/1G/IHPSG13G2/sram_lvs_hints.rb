# Structural search hints; qualification also requires the independent Python graph audit.
  # Label only isolated two-MOS filler loads. No device or connection is changed.
  filler_groups = lambda do |c|
    groups = Hash.new { |h, k| h[k] = [] }
    key = lambda { |n| n ? n.expanded_name.upcase : nil }
    c.each_device do |n|
      next unless n.device_class.name.downcase == 'sg13_lv_nmos'
      ng, nb = n.net_for_terminal('G'), n.net_for_terminal('B')
      next unless key.call(nb) == 'VSS!'
      ns, nd = n.net_for_terminal('S'), n.net_for_terminal('D')
      internal = if key.call(ns) == key.call(nb) then nd elsif key.call(nd) == key.call(nb) then ns else nil end
      next unless ng && internal
      next unless [ng, internal].all? { |z| z.pin_count == 0 && z.subcircuit_pin_count == 0 && z.terminal_count == 2 }
      peers = [ng, internal].flat_map { |z| z.each_terminal.map(&:device) }.uniq(&:id)
      next unless peers.size == 2 && peers.any? { |d| d.id == n.id }
      p = peers.find { |d| d.id != n.id }
      next unless p.device_class.name.downcase == 'sg13_lv_pmos'
      pb, pg = p.net_for_terminal('B'), p.net_for_terminal('G')
      next unless key.call(pb) == 'VDD!'
      ps, pd = p.net_for_terminal('S'), p.net_for_terminal('D')
      pi = if key.call(ps) == key.call(pb) then pd elsif key.call(pd) == key.call(pb) then ps else nil end
      next unless key.call(pi) == key.call(ng) && key.call(pg) == key.call(internal)
      signature = [n.device_class.name, p.device_class.name, key.call(nb), key.call(pb)] + [n, p].flat_map { |d| ['L', 'W'].map { |v| format('%.12g', d.parameter(v)) } }
      groups[signature] << [n.id, p.id, ng, internal]
    end
    groups
  end
  netlist.each_circuit do |left|
    next unless ["RM_IHPSG13_2P_1024X16_C2_BM_BIST", "RM_IHPSG13_1024X16_C2_2P_ROWDEC8", "RM_IHPSG13_1024X16_C2_2P_ROWREG8", "RM_IHPSG13_1024X16_C2_2P_DEC04"].include?(left.name.upcase)
    right = schematic.each_circuit.find { |c| c.name.upcase == left.name.upcase }
    next unless right
    a, b = filler_groups.call(left), filler_groups.call(right)
    next if a.empty? && b.empty?
    raise "different filler load inventory in #{left.name}" unless a.transform_values(&:size) == b.transform_values(&:size)
    [left, right].each do |c|
      raise 'reserved filler net name present' if c.each_net.any? { |n| n.name.start_with?('__CPPHDL_FILLCAP_') }
    end
    [a, b].each do |groups|
      groups.keys.sort.each_with_index do |signature, family|
        groups[signature].sort_by { |entry| entry[0] }.each_with_index do |entry, index|
          entry[2..3].each_with_index do |net, node|
            before = net.each_terminal.map { |t| [t.device.id, t.terminal_id] }.sort
            net.name = "__CPPHDL_FILLCAP_#{family}_#{index}_#{node}"
            after = net.each_terminal.map { |t| [t.device.id, t.terminal_id] }.sort
            raise 'filler label changed connectivity' unless before == after && net.pin_count == 0 && net.subcircuit_pin_count == 0
          end
        end
      end
    end
    logger.info("CPPHDL: labeled #{a.values.sum(&:size)} isolated filler loads in #{left.name}; all terminals preserved")
  end
  # Diagnostic mapping hints. A separate endpoint audit is required before acceptance.
  netlist.each_circuit do |left|
    right = schematic.each_circuit.find { |c| c.name.upcase == left.name.upcase }
    next unless right
    left.each_pin do |pin|
      other = right.pin_by_name(pin.name)
      next unless other
      a, b = left.net_for_pin(pin), right.net_for_pin(other)
      same_nets(left.name, a.name, b.name) unless a.name.empty? || b.name.empty?
    end
    same_nets(left.name, '__CPPHDL_FILLCAP_*')
  end
