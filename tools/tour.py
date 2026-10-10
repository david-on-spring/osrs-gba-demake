#!/usr/bin/env python3
"""Writes a harness script that teleports through every area and screenshots it.
usage: tour.py out_script.txt shot_dir"""
import sys
PLACES = [
    ('lumbridge_castle', 3222, 3218), ('lumbridge_store', 3214, 3238), ('lumbridge_church', 3233, 3210),
    ('lumbridge_bridge', 3241, 3230), ('cow_field', 3250, 3272), ('lumbridge_swamp', 3230, 3156),
    ('draynor_bank', 3093, 3250), ('draynor_market', 3078, 3250), ('draynor_willows', 3090, 3236),
    ('draynor_manor', 3108, 3346), ('wizards_tower', 3109, 3176), ('port_sarim', 3032, 3232),
    ('rimmington', 2958, 3220), ('falador_centre', 2990, 3362), ('falador_park', 3008, 3370),
    ('falador_east_bank', 3014, 3364), ('falador_west_bank', 2953, 3368), ('white_knights', 2971, 3356),
    ('doric', 2958, 3450), ('goblin_village', 2957, 3492), ('ice_mountain', 3024, 3444),
    ('barbarian_village', 3084, 3424), ('edgeville_bank', 3094, 3484), ('edgeville_yews', 3086, 3473),
    ('wilderness', 3094, 3522), ('grand_exchange', 3165, 3477), ('varrock_square', 3212, 3423),
    ('varrock_west_bank', 3195, 3440), ('varrock_east_bank', 3253, 3428), ('varrock_palace', 3212, 3464),
    ('varrock_church', 3246, 3478), ('champions_guild', 3192, 3370), ('varrock_se_mine', 3286, 3366),
    ('dark_wizards', 3225, 3380), ('al_kharid_gate', 3262, 3228), ('al_kharid_palace', 3292, 3184),
    ('al_kharid_bank', 3279, 3166), ('al_kharid_mine', 3297, 3296), ('duel_arena', 3310, 3232),
]
out, d = sys.argv[1], sys.argv[2]
L = ['wait 60', 'press A', 'wait 30']
for name, X, Y in PLACES:
    L += ['poke16 @pl %x' % X, 'poke16 @pl+2 %x' % Y, 'poke16 @pl+4 %x' % ((X - 2928) * 16), 'poke16 @pl+6 %x' % ((3551 - Y) * 16),
          'wait 12', 'shot %s/%s.png' % (d, name), 'dump @cache_misses 1']
open(out, 'w').write('\n'.join(L) + '\n')
