#!/usr/bin/env python3
"""Generate sample albums with proper ID3 tags for Rockbox simulator testing."""
import os
import shutil
from mutagen.id3 import ID3, TIT2, TPE1, TPE2, TALB, TRCK, TPOS, TDRC, TCON

SRC = os.path.expanduser("~/Temp/file_example_MP3_1MG.mp3")
DEST = os.path.expanduser("~/Source/rockbox/build-sim/simdisk/Music")

ALBUMS = [
    {
        "artist": "Sonic Youth",
        "album": "A Thousand Leaves",
        "year": "1998",
        "genre": "Alternative",
        "tracks": [
            "Contre le Sexisme",
            "Sunday",
            "Female Mechanic Now on Duty",
            "Wildflower Soul",
            "Hoarfrost",
            "French Tickler",
            "Hits of Sunshine",
            "Karen Koltrane",
            "The Ineffable Me",
            "Snare Girl",
            "Heather Angel",
            "Karen Revisited",
        ],
    },
    {
        "artist": "Radiohead",
        "album": "OK Computer",
        "year": "1997",
        "genre": "Alternative",
        "tracks": [
            "Airbag",
            "Paranoid Android",
            "Subterranean Homesick Alien",
            "Exit Music (For a Film)",
            "Let Down",
            "Karma Police",
            "Fitter Happier",
            "Electioneering",
            "Climbing Up the Walls",
            "No Surprises",
            "Lucky",
            "The Tourist",
        ],
    },
    {
        "artist": "Portishead",
        "album": "Dummy",
        "year": "1994",
        "genre": "Trip Hop",
        "tracks": [
            "Mysterons",
            "Sour Times",
            "Strangers",
            "It Could Be Sweet",
            "Wandering Star",
            "It's a Fire",
            "Numb",
            "Roads",
            "Pedestal",
            "Biscuit",
            "Glory Box",
        ],
    },
    {
        "artist": "Boards of Canada",
        "album": "Music Has the Right to Children",
        "year": "1998",
        "genre": "Electronic",
        "tracks": [
            "Wildlife Analysis",
            "An Eagle in Your Mind",
            "The Color of the Fire",
            "Telephasic Workshop",
            "Triangles & Rhombuses",
            "Sixtyten",
            "Turquoise Hexagon Sun",
            "Kaini Industries",
            "Bocuma",
            "Roygbiv",
            "Rue the Whirl",
            "Aquarius",
            "Olson",
            "Pete Standing Alone",
            "Smokes Quantity",
            "Open the Light",
            "One Very Important Thought",
            "Happy Cycling",
        ],
    },
    {
        "artist": "Massive Attack",
        "album": "Mezzanine",
        "year": "1998",
        "genre": "Trip Hop",
        "tracks": [
            "Angel",
            "Risingson",
            "Teardrop",
            "Inertia Creeps",
            "Exchange",
            "Dissolved Girl",
            "Man Next Door",
            "Black Milk",
            "Mezzanine",
            "Group Four",
            "(Exchange)",
        ],
    },
]

os.makedirs(DEST, exist_ok=True)

count = 0
for album_info in ALBUMS:
    album_dir = os.path.join(DEST, album_info["artist"], album_info["album"])
    os.makedirs(album_dir, exist_ok=True)

    for i, title in enumerate(album_info["tracks"], 1):
        fname = f"{i:02d} {title}.mp3"
        fpath = os.path.join(album_dir, fname)
        shutil.copy2(SRC, fpath)

        tags = ID3()
        tags.add(TIT2(encoding=3, text=title))
        tags.add(TPE1(encoding=3, text=album_info["artist"]))
        tags.add(TPE2(encoding=3, text=album_info["artist"]))
        tags.add(TALB(encoding=3, text=album_info["album"]))
        tags.add(TRCK(encoding=3, text=f"{i}/{len(album_info['tracks'])}"))
        tags.add(TPOS(encoding=3, text="1/1"))
        tags.add(TDRC(encoding=3, text=album_info["year"]))
        tags.add(TCON(encoding=3, text=album_info["genre"]))
        tags.save(fpath)
        count += 1

print(f"Created {count} tracks across {len(ALBUMS)} albums in {DEST}")
