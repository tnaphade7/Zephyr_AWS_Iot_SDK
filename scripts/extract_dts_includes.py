#!/usr/bin/env python
import sys
from os import walk
import os
import re
import yaml
import pprint

from devicetree import parse_file

# globals
compatibles = {}
phandles = {}
aliases = {}
chosen = {}
reduced = {}

def convert_string_to_label(s):
  # Transmute ,- to _
  s = s.replace("-", "_");
  s = s.replace(",", "_");
  return s

def get_all_compatibles(d, name, comp_dict):
  if 'props' in d:
    compat = d['props'].get('compatible')
    enabled = d['props'].get('status')

  if enabled == "disabled":
    return comp_dict

  if compat != None:
    comp_dict[name] = compat

  if name != '/':
    name += '/'

  if isinstance(d,dict):
    if d['children']:
      for k,v in d['children'].items():
        get_all_compatibles(v, name + k, comp_dict)

  return comp_dict

def get_aliases(root):
  if 'children' in root:
    if 'aliases' in root['children']:
      for k,v in root['children']['aliases']['props'].items():
        aliases[v] = k

  return

def get_compat(node):

  compat = None

  if 'props' in node:
    compat = node['props'].get('compatible')

  if isinstance(compat, list):
    compat = compat[0]

  return compat

def get_chosen(root):

  if 'children' in root:
    if 'chosen' in root['children']:
      for k,v in root['children']['chosen']['props'].items():
        chosen[k] = v

  return

def get_phandles(root, name, handles):

  if 'props' in root:
    handle = root['props'].get('phandle')
    enabled = root['props'].get('status')

  if enabled == "disabled":
    return

  if handle != None:
    phandles[handle] = name

  if name != '/':
    name += '/'

  if isinstance(root, dict):
    if root['children']:
      for k,v in root['children'].items():
        get_phandles(v, name + k, handles)

  return

class Loader(yaml.Loader):
    def __init__(self, stream):
        self._root = os.path.realpath(stream.name)
        super(Loader, self).__init__(stream)
        Loader.add_constructor('!include', Loader.include)
        Loader.add_constructor('!import',  Loader.include)

    def include(self, node):
        if isinstance(node, yaml.ScalarNode):
            return self.extractFile(self.construct_scalar(node))

        elif isinstance(node, yaml.SequenceNode):
            result = []
            for filename in self.construct_sequence(node):
                result += self.extractFile(filename)
            return result

        elif isinstance(node, yaml.MappingNode):
            result = {}
            for k,v in self.construct_mapping(node).iteritems():
                result[k] = self.extractFile(v)
            return result

        else:
            print("Error:: unrecognised node type in !include statement")
            raise yaml.constructor.ConstructorError

    def extractFile(self, filename):
        filepath = os.path.join(os.path.dirname(self._root), filename)
        if not os.path.isfile(filepath):
            # we need to look in common directory
            # take path and back up 2 directories and tack on '/common/yaml'
            filepath = os.path.dirname(self._root).split('/')
            filepath = '/'.join(filepath[:-2])
            filepath = os.path.join(filepath + '/common/yaml', filename)
            with open(filepath, 'r') as f:
                return yaml.load(f, Loader)

def insert_defs(node_address, defs, new_defs, new_aliases):
  if node_address in defs:
    if 'aliases' in defs[node_address]:
      defs[node_address]['aliases'].update(new_aliases)
    else:
      defs[node_address]['aliases'] = new_aliases

    defs[node_address].update(new_defs)
  else:
    new_defs['aliases'] = new_aliases
    defs[node_address] = new_defs

  return

def find_node_by_path(nodes, path):
  d = nodes
  for k in path[1:].split('/'):
    d = d['children'][k]

  return d

def compress_nodes(nodes, path):
  if 'props' in nodes:
    status = nodes['props'].get('status')

  if status == "disabled":
    return

  if isinstance(nodes, dict):
    reduced[path] = dict(nodes)
    reduced[path].pop('children', None)
    if path != '/':
      path += '/'
    if nodes['children']:
      for k,v in nodes['children'].items():
        compress_nodes(v, path + k)

  return

def find_parent_irq_node(node_address):
  address = ''

  for comp in node_address.split('/')[1:]:
    address += '/' + comp
    if 'interrupt-parent' in reduced[address]['props']:
      interrupt_parent = reduced[address]['props'].get('interrupt-parent')

  return reduced[phandles[interrupt_parent]]

def extract_interrupts(node_address, yaml, y_key, names, defs, def_label):
  node = reduced[node_address]

  try:
    props = list(node['props'].get(y_key))
  except:
    props = [node['props'].get(y_key)]

  irq_parent = find_parent_irq_node(node_address)

  l_base = def_label.split('/')
  index = 0

  while props:
    prop_def = {}
    prop_alias = {}
    l_idx = [str(index)]

    if y_key == 'interrupts-extended':
      cell_parent = reduced[phandles[props.pop(0)]]
      name = []
    else:
      try:
        name = [names.pop(0).upper()]
      except:
        name = []

      cell_parent = irq_parent

    cell_yaml = yaml[get_compat(cell_parent)]
    l_cell_prefix = [yaml[get_compat(irq_parent)].get('cell_prefix', []).upper()]

    for i in range(cell_parent['props']['#interrupt-cells']):
      l_cell_name = [cell_yaml['#cells'][i].upper()]
      if l_cell_name == l_cell_prefix:
        l_cell_name = []

      l_fqn = '_'.join(l_base + l_cell_prefix + l_idx)
      prop_def[l_fqn] = props.pop(0)
      if len(name):
        prop_alias['_'.join(l_base + name + l_cell_prefix)] = l_fqn

    index += 1
    insert_defs(node_address, defs, prop_def, prop_alias)

  return

def extract_reg_prop(node_address, names, defs, def_label, div):
  node = reduced[node_address]

  props = list(reduced[node_address]['props']['reg'])

  address_cells = reduced['/']['props'].get('#address-cells')
  size_cells = reduced['/']['props'].get('#size-cells')
  address = ''
  for comp in node_address.split('/')[1:]:
    address += '/' + comp
    address_cells = reduced[address]['props'].get('#address-cells', address_cells)
    size_cells = reduced[address]['props'].get('#size-cells', size_cells)

  index = 0
  l_base = def_label.split('/')
  l_addr = ["BASE_ADDRESS"]
  l_size = ["SIZE"]

  while props:
    prop_def = {}
    prop_alias = {}
    addr = 0
    size = 0
    l_idx = [str(index)]

    try:
      name = [names.pop(0).upper()]
    except:
      name = []

    for x in range(address_cells):
      addr += props.pop(0) << (32 * x)
    for x in range(size_cells):
      size += props.pop(0) << (32 * x)

    l_addr_fqn = '_'.join(l_base + l_addr + l_idx)
    l_size_fqn = '_'.join(l_base + l_size + l_idx)
    prop_def[l_addr_fqn] = hex(addr)
    prop_def[l_size_fqn] = int(size / div)
    if len(name):
      prop_alias['_'.join(l_base + name + l_addr)] = l_addr_fqn
      prop_alias['_'.join(l_base + name + l_size)] = l_size_fqn

    if index == 0:
      prop_alias['_'.join(l_base + l_addr)] = l_addr_fqn
      prop_alias['_'.join(l_base + l_size)] = l_size_fqn

    insert_defs(node_address, defs, prop_def, prop_alias)

    # increment index for definition creation
    index += 1

  return

def extract_cells(node_address, yaml, y_key, names, index, prefix, defs, def_label):
  try:
    props = list(reduced[node_address]['props'].get(y_key))
  except:
    props = [reduced[node_address]['props'].get(y_key)]

  cell_parent = reduced[phandles[props.pop(0)]]

  try:
    cell_yaml = yaml[get_compat(cell_parent)]
  except:
    raise Exception("Could not find yaml description for " + cell_parent['name'])

  try:
    name = names.pop(0).upper()
  except:
    name = []

  l_cell = [str(cell_yaml.get('cell_string',''))]
  l_base = def_label.split('/')
  l_base += prefix
  l_idx = [str(index)]

  prop_def = {}
  prop_alias = {}

  for k in cell_parent['props'].keys():
    if k[0] == '#' and '-cells' in k:
      for i in range(cell_parent['props'].get(k)):
        l_cellname = [str(cell_yaml['#cells'][i]).upper()]
        if l_cell == l_cellname:
          label = l_base + l_cell + l_idx
        else:
          label = l_base + l_cell + l_cellname + l_idx
        label_name = l_base + name + l_cellname
        prop_def['_'.join(label)] = props.pop(0)
        if len(name):
          prop_alias['_'.join(label_name)] = '_'.join(label)

        if index == 0:
          prop_alias['_'.join(label[:-1])] = '_'.join(label)

    insert_defs(node_address, defs, prop_def, prop_alias)

  # recurse if we have anything left
  if len(props):
    extract_cells(node_address, yaml, y_key, names, index + 1, prefix, defs, def_label)

  return

def extract_pinctrl(node_address, yaml, pinconf, names, index, defs, def_label):

  prop_list = []
  if not isinstance(pinconf,list):
    prop_list.append(pinconf)
  else:
    prop_list = list(pinconf)

  def_prefix = def_label.split('_')
  target_node = node_address

  prop_def = {}
  for p in prop_list:
    pin_node_address = phandles[p]
    pin_entry = reduced[pin_node_address]
    parent_address = '/'.join(pin_node_address.split('/')[:-1])
    pin_parent = reduced[parent_address]
    cell_yaml = yaml[get_compat(pin_parent)]
    cell_prefix = cell_yaml.get('cell_string', None)
    post_fix = []

    if cell_prefix != None:
      post_fix.append(cell_prefix)

    for subnode in reduced.keys():
      if pin_node_address in subnode and pin_node_address != subnode:
        # found a subnode underneath the pinmux handle
        node_label = subnode.split('/')[-2:]
        pin_label = def_prefix + post_fix + subnode.split('/')[-2:]

        for i, pin in enumerate(reduced[subnode]['props']['pins']):
          key_label = list(pin_label) + [cell_yaml['#cells'][0]] + [str(i)]
          func_label = key_label[:-2] + [cell_yaml['#cells'][1]] + [str(i)]
          key_label = convert_string_to_label('_'.join(key_label)).upper()
          func_label = convert_string_to_label('_'.join(func_label)).upper()

          prop_def[key_label] = pin
          prop_def[func_label] = reduced[subnode]['props']['function']

  insert_defs(node_address, defs, prop_def, {})

def extract_single(node_address, yaml, prop, key, prefix, defs, def_label):

  prop_def = {}

  if isinstance(prop, list):
    for i, p in enumerate(prop):
      k = convert_string_to_label(key).upper()
      label = def_label + '_' + k
      prop_def[label + '_' + str(i)] = p
  else:
      k = convert_string_to_label(key).upper()
      label = def_label + '_' +  k
      prop_def[label] = prop

  if node_address in defs:
    defs[node_address].update(prop_def)
  else:
    defs[node_address] = prop_def

  return

def extract_property(yaml, node_address, y_key, y_val, names, prefix, defs):

  node = reduced[node_address]
  def_label = convert_string_to_label(get_compat(node)).upper()
  def_label += '_' + node_address.split('@')[-1].upper()

  if y_key == 'reg':
    extract_reg_prop(node_address, names, defs, def_label, 1)
  elif y_key == 'interrupts' or y_key == 'interupts-extended':
    extract_interrupts(node_address, yaml, y_key, names, defs, def_label)
  elif 'pinctrl-' in y_key:
    p_index = int(y_key.split('-')[1])
    extract_pinctrl(node_address, yaml, reduced[node_address]['props'][y_key],
                    names[p_index], p_index, defs, def_label)
  elif 'clocks' in y_key:
    extract_cells(node_address, yaml, y_key,
                  names, 0, prefix, defs, def_label)
  else:
    extract_single(node_address, yaml[get_compat(reduced[node_address])],
                   reduced[node_address]['props'][y_key], y_key,
                   prefix, defs, def_label)

  return

def extract_node_include_info(reduced, node_address, yaml, defs, structs):
  node = reduced[node_address]
  node_compat = get_compat(node)

  if not node_compat in yaml.keys():
    return {}, {}

  y_node = yaml[node_compat]

  # check to see if we need to process the properties
  for yp in y_node['properties']:
    for k,v in yp.items():
      if 'generation' in v:
        if v['generation'] == 'define':
          label = v.get('define_string')
          storage = defs
        else:
          label = v.get('structures_string')
          storage = structs

        prefix = []
        if v.get('use-name-prefix') != None:
          prefix = [convert_string_to_label(k.upper())]

        for c in node['props'].keys():
          if c.endswith("-names"):
            pass

          if re.match(k + '$', c):

            if 'pinctrl-' in c:
              names = node['props'].get('pinctrl-names', [])
            else:
              names = node['props'].get(c[:-1] + '-names', [])
              if not names:
                names = node['props'].get(c + '-names', [])

            if not isinstance(names, list):
              names = [names]

            extract_property(yaml, node_address, c, v, names, prefix, defs)

  return

def yaml_collapse(yaml_list):
  collapsed = dict(yaml_list)

  for k,v in collapsed.items():
    props = set()
    if 'properties' in v:
       for entry in v['properties']:
         for key in entry:
           props.add(key)

    if 'inherits' in v:
      for inherited in v['inherits']:
        for prop in inherited['properties']:
          for key in prop:
            if key not in props:
              v['properties'].append(prop)
      v.pop('inherits')

  return collapsed


def print_key_value(k, v, tabstop):
  label = "#define " + k

  # calculate the name's tabs
  if len(label) % 8:
    tabs = (len(label) + 7)  >> 3
  else:
    tabs = (len(label) >> 3) + 1

  sys.stdout.write(label)
  for i in range(0, tabstop - tabs + 1):
    sys.stdout.write('\t')
  sys.stdout.write(str(v))
  sys.stdout.write("\n")

  return

def generate_include_file(defs):
    compatible = reduced['/']['props']['compatible'][0]

    sys.stdout.write("/**************************************************\n")
    sys.stdout.write(" * Generated include file for " + compatible)
    sys.stdout.write("\n")
    sys.stdout.write(" *               DO NOT MODIFY\n");
    sys.stdout.write(" */\n")
    sys.stdout.write("\n")
    sys.stdout.write("#ifndef _DEVICE_TREE_BOARD_H" + "\n");
    sys.stdout.write("#define _DEVICE_TREE_BOARD_H" + "\n");
    sys.stdout.write("\n")

    node_keys = sorted(defs.keys())
    for node in node_keys:
       sys.stdout.write('/* ' + node.split('/')[-1] + ' */')
       sys.stdout.write("\n")

       maxlength = max(len(s + '#define ') for s in defs[node].keys())
       if maxlength % 8:
           maxtabstop = (maxlength + 7) >> 3
       else:
         maxtabstop = (maxlength >> 3) + 1

       if (maxtabstop * 8 - maxlength) <= 2:
         maxtabstop += 1

       prop_keys = sorted(defs[node].keys())
       for prop in prop_keys:
         if prop == 'aliases':
           for entry in sorted(defs[node][prop]):
             print_key_value(entry, defs[node][prop].get(entry), maxtabstop)
         else:
           print_key_value(prop, defs[node].get(prop), maxtabstop)
       sys.stdout.write("\n")

    sys.stdout.write("#endif\n");

def main(args):
  if len(args) < 2:
    print('Usage: %s filename.dts path_to_yaml' % args[0])
    return 1

  try:
    with open(args[1], "r") as fd:
      d = parse_file(fd)
  except:
     raise Exception("Input file " + os.path.abspath(args[1]) + " does not exist.")

  # compress list to nodes w/ paths, add interrupt parent
  compress_nodes(d['/'], '/')

  # build up useful lists
  compatibles = get_all_compatibles(d['/'], '/', {})
  get_phandles(d['/'], '/', {})
  get_aliases(d['/'])
  get_chosen(d['/'])

  # find unique set of compatibles across all active nodes
  s = set()
  for k,v in compatibles.items():
    if isinstance(v,list):
      for item in v:
        s.add(item)
    else:
      s.add(v)

  # scan YAML files and find the ones we are interested in
  yaml_files = []
  for (dirpath, dirnames, filenames) in walk(args[2]):
    yaml_files.extend([f for f in filenames if re.match('.*\.yaml\Z', f)])
    yaml_files = [dirpath + '/' + t for t in yaml_files]
    break

  yaml_list = {}
  file_load_list = set()
  for file in yaml_files:
    for line in open(file, 'r'):
      if re.search('^\s+constraint:*', line):
        c = line.split(':')[1].strip()
        c = c.strip('"')
        if c in s:
          if not file in file_load_list:
            file_load_list.add(file)
            with open(file, 'r') as yf:
              yaml_list[c] = yaml.load(yf, Loader)

  if yaml_list == {}:
    raise Exception("Missing YAML information.  Check YAML sources")

  # collapse the yaml inherited information
  yaml_list = yaml_collapse(yaml_list)

  # load zephyr specific nodes
  flash = {}
  console = {}
  sram = {}
  if 'zephyr,flash' in chosen:
    flash = reduced[chosen['zephyr,flash']]
  if 'zephyr,console' in chosen:
    console = reduced[chosen['zephyr,console']]
  if 'zephyr,sram' in chosen:
    sram = reduced[chosen['zephyr,sram']]

  defs = {}
  structs = {}
  for k, v in reduced.items():
    node_compat = get_compat(v)
    if node_compat != None and node_compat in yaml_list:
      extract_node_include_info(reduced, k, yaml_list, defs, structs)

  if defs == {}:
    raise Exception("No information parsed from dts file.")

  if flash:
    extract_reg_prop(chosen['zephyr,flash'], None, defs, "CONFIG_FLASH", 1024)
  else:
    # We will add address and size of 0 for systems with no flash controller
    # This is what they already do in the Kconfig options anyway
    defs['dummy-flash'] =  { 'CONFIG_FLASH_BASE_ADDRESS': 0, 'CONFIG_FLASH_SIZE': 0 }

  if sram:
    extract_reg_prop(chosen['zephyr,sram'], None, defs, "CONFIG_SRAM", 1024)

  # generate include file
  generate_include_file(defs)

if __name__ == '__main__':
  # test1.py executed as script
  # do something
  sys.exit(main(sys.argv))
