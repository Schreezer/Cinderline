from pathlib import Path
from collections import Counter
import bpy,json,math
from mathutils import Vector
root=Path.cwd()
report={}
def inspect(obj):
 mesh=obj.data
 mesh.calc_loop_triangles()
 counts={str(t):0 for t in (0,1e-14,1e-10,1e-8,1e-6,.00005,1e-4,.001,.01)}
 coincident=0
 examples=[]
 for tri in mesh.loop_triangles:
  coords=[obj.matrix_world@mesh.vertices[i].co for i in tri.vertices]
  area=(coords[1]-coords[0]).cross(coords[2]-coords[0]).length*.5
  if any(all(abs(coords[a][i]-coords[b][i])<=.00002 for i in range(3)) for a,b in ((0,1),(1,2),(2,0))): coincident+=1
  for k in counts:
   if area<=float(k): counts[k]+=1
  if area<1e-6:
   examples.append({'material':tri.material_index,'area':area,'coords':[list(c) for c in coords]})
 return {'triangles':len(mesh.loop_triangles),'vertices':len(mesh.vertices),'counts_below_area_cm2':counts,'coincident_corner_triangles':coincident,'tiny_triangles':examples}
for obj in bpy.data.objects:
 if obj.type=='MESH': report[obj.name]=inspect(obj)
bpy.ops.object.select_all(action='DESELECT')
bpy.ops.import_scene.fbx(filepath=str(root/'RawAssets/Models/FBX/SM_Skim.fbx'))
for obj in bpy.context.selected_objects:
 if obj.type=='MESH': report['Skim_FBX']=inspect(obj)
source=(root/'scripts/create_blender_assets.py').read_text()
prefix=source.split('SPECS = [')[0]
ns={'__file__':str(root/'scripts/create_blender_assets.py')}
exec(compile(prefix,'create_blender_assets_prefix','exec'),ns)
ns['skim']()
parts=[]
for i,obj in enumerate(ns['PARTS']):
 info=inspect(obj)
 if info['counts_below_area_cm2']['1e-10']:
  parts.append({'part':i,'name':obj.name,'location':list(obj.location),'dimensions':list(obj.dimensions),'material':obj.data.materials[0].name,'stats':info})
report['skim_degenerate_parts_authored_coordinates']=parts
(root/'artifacts/models/geometry-diagnostics.json').write_text(json.dumps(report,indent=2)+'\n')
print('CINDER_GEOMETRY_DIAG',json.dumps({k:({a:b for a,b in v.items() if a!='tiny_triangles'} if isinstance(v,dict) else [{'part':p['part'],'name':p['name'],'location':p['location'],'counts':p['stats']['counts_below_area_cm2']} for p in v]) for k,v in report.items()}))
