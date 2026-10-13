# Compares tag field offsets between the Halo 3 MCC and ODST MCC Assembly
# plugins, by name path (block names / field names).
import sys
import xml.etree.ElementTree as ET

H3 = r'..\h3'
OD = r'xml'


def flatten(path):
    out = {}

    def walk(e, prefix):
        for c in e:
            off = c.get('offset')
            if off is None: continue
            name = (c.get('name') or c.get('title') or '').strip()
            key = prefix + (name,)
            size = c.get('elementSize') or c.get('entrySize')
            if key not in out: out[key] = (int(off, 16), c.tag, size and int(size, 16))
            if c.tag in ('tagblock', 'reflexive'): walk(c, key)
    walk(ET.parse(path).getroot(), ())
    return out


WANT = {
    'bipd': [('Jump Velocity',)],
    'vehi': [('Boost Peak Time',), ('Boost Dead Time',), ('Seats',), ('Seats', 'Flags'), ('Seats', 'Boarding Seat Index'),
             ('Seats', 'Turnover Time'), ('EMP Disabled Time',),
             ('Human Tank Type',), ('Human Jeep Type',), ('Human Plane Type',), ('Alien Scout Type',),
             ('Alien Fighter Type',), ('Mantis Type',), ('VTOL Type',), ('Chopper Type',), ('Guardian Type',)],
    'weap': [('Flags',), ('Magazines',), ('Magazines', 'Rounds Total Maximum'), ('Magazines', 'Rounds Loaded Maximum'),
             ('Autoaim Angle',), ('Autoaim Range',), ('Autoaim Falloff Range',), ('Deviation Angle',), ('Barrels',),
             ('Barrels', 'Projectile'), ('Barrels', 'Firing Effects'), ('Barrels', 'Firing Effects', 'Firing Effect'),
             ('Barrels', 'Firing Effects', 'Firing Damage'), ('Barrels', 'Firing Effects', 'Firing Rider Damage')],
    'effe': [('Events',), ('Events', 'Accelerations'), ('Events', 'Accelerations', 'Acceleration')],
    'eqip': [('Number Of Uses',), ('Uses',)],
    'matg': [('Grenades',), ('Grenades', 'Maximum Count'), ('Player Information',)],
    'scnr': [('Soft Ceilings',), ('Kill Triggers',)],
}

for group, keys in WANT.items():
    h3file = {'effe': 'effe_Halo3.xml'}.get(group, group + '.xml')
    a = flatten(f'{H3}\\{h3file}')
    b = flatten(f'{OD}\\{group}.xml')
    for k in keys:
        x, y = a.get(k), b.get(k)
        fmt = lambda v: 'missing' if v is None else f'{v[0]:#x}' + (f' [size {v[2]:#x}]' if v[2] else '')
        flag = '' if x and y and x[0] == y[0] and x[2] == y[2] else '   <-- differs'
        print(f'{group:5} {" / ".join(k):45} H3 {fmt(x):22} ODST {fmt(y):22}{flag}')
