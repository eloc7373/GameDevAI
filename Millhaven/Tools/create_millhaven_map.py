"""
Creates Content/Maps/Millhaven - the empty level the project's config expects.

This replaces the manual "File > New Level > Empty Level > Save As" step.

How to run it:
  1. In the Unreal Editor, enable Edit > Plugins > "Python Editor Script Plugin"
     and restart the editor when prompted.
  2. Window > Developer Tools > Output Log, switch the console dropdown from
     "Cmd" to "Python", then run:

         exec(open(r"<project>/Tools/create_millhaven_map.py").read())

     Or from a terminal:

         UnrealEditor-Cmd.exe "<project>/Millhaven.uproject" \
             -run=pythonscript -script="<project>/Tools/create_millhaven_map.py"

Nothing needs to be placed in the level by hand. AMillhavenGameMode spawns the
world generator, which builds terrain, village, NPCs and sky at BeginPlay.
"""

import unreal

MAP_PATH = "/Game/Maps/Millhaven"


def create_map() -> bool:
    subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        unreal.log_warning(
            "Millhaven: {} already exists - leaving it alone.".format(MAP_PATH)
        )
        return True

    if not subsystem.new_level(MAP_PATH):
        unreal.log_error("Millhaven: could not create {}.".format(MAP_PATH))
        return False

    unreal.log(
        "Millhaven: created {}. It is intentionally empty - press Play and the "
        "world generates itself.".format(MAP_PATH)
    )
    return True


if __name__ == "__main__":
    create_map()
