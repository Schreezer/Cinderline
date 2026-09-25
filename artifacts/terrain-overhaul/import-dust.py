"""Unreal editor entry point: rebuild only M_VT_Dust, then write its report."""
import importlib.util
import json
import traceback
from pathlib import Path
import unreal

root = Path(__file__).resolve().parents[2]
source = root / 'scripts/unreal_visual_target_materials.py'
spec = importlib.util.spec_from_file_location('cinder_dust_repair', source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
report = {'success': False, 'scope': '/Game/Art/VisualTarget/Materials/M_VT_Dust'}
try:
    helper = module._load_graph_helper()
    report['source_sha256'] = helper.sha256(source)
    report['material'] = module._dust_material(helper)
    report['success'] = True
except Exception as error:
    report.update(error=str(error), traceback=traceback.format_exc())
    raise
finally:
    (root / 'artifacts/terrain-overhaul/dust-import.json').write_text(json.dumps(report, indent=2) + '\n')
unreal.log('CINDERLINE_DUST_REPAIR_OK ' + report['scope'])
