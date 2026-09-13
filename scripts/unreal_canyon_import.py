"""Import the owned portrait/canyon kit, author Landscapes, and finish shaders.

Run in a render-capable UnrealEditor-Cmd process, not with -NullRHI. Existing
non-Cinderline actors and unrelated assets are retained. Stage each caller's
build/import/render operations serially to keep Mac resource usage bounded.
"""
import importlib.util
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def import_canyon():
    target = ROOT / 'artifacts/canyon-hud/landscape-import.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    report = {'success': False}
    try:
        portraits = module('unreal_unit_portraits').import_unit_portraits()
        module('unreal_canyon_assets').run()
        module('unreal_canyon_surface').build_canyon_surface()
        authored = unreal.CinderLandscapeAuthoringLibrary.author_frontier_landscapes()
        assert authored and authored.startswith('Authored three'), authored
        finalized = unreal.CinderLandscapeAuthoringLibrary.finalize_canyon_assets()
        assert finalized and finalized.startswith('Finalized 3'), finalized
        landscapes = []
        for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
            if isinstance(actor, unreal.Landscape) and any(str(tag).startswith('CinderLandscapeMap') for tag in actor.tags):
                landscapes.append({'label': actor.get_actor_label(), 'tags': [str(t) for t in actor.tags],
                    'components': len(actor.get_components_by_class(unreal.LandscapeComponent)),
                    'scale': str(actor.get_actor_scale3d()),
                    'material': actor.get_editor_property('landscape_material').get_path_name()})
        assert len(landscapes) == 3 and all(x['components'] == 4 for x in landscapes), landscapes
        report.update(success=True, authoring=authored, shaderValidation=finalized,
                      landscapes=landscapes, portraits=portraits)
    except Exception as error:
        report.update(error=str(error), traceback=traceback.format_exc())
        raise
    finally:
        target.write_text(json.dumps(report, indent=2) + '\n')
    unreal.log('CINDERLINE_CANYON_IMPORT_OK ' + str(target))
    return report


if __name__ == '__main__':
    import_canyon()
