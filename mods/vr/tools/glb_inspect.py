#!/usr/bin/env python3
"""Inspetor de GLB/glTF sem dependencias: estrutura, skin, ossos, bbox e pose.

Uso: python3 glb_inspect.py arquivo.glb
"""
import json, struct, sys, math

def load_glb(path):
    data = open(path, 'rb').read()
    magic, ver, length = struct.unpack('<III', data[:12])
    assert magic == 0x46546C67, "nao e GLB (magic errado)"
    off = 12
    js = None
    bins = []
    while off < length:
        clen, ctype = struct.unpack('<II', data[off:off+8])
        chunk = data[off+8:off+8+clen]
        if ctype == 0x4E4F534A:
            js = json.loads(chunk.decode('utf-8'))
        elif ctype == 0x004E4942:
            bins.append(chunk)
        off += 8 + clen
    return js, (bins[0] if bins else b'')

CT = {5120:('b',1),5121:('B',1),5122:('h',2),5123:('H',2),5125:('I',4),5126:('f',4)}
NC = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}

def acc_read(g, js, i):
    a = js['accessors'][i]
    fmt, sz = CT[a['componentType']]
    n = NC[a['type']]
    bv = js['bufferViews'][a['bufferView']]
    base = bv.get('byteOffset',0) + a.get('byteOffset',0)
    stride = bv.get('byteStride') or (sz*n)
    out = []
    for k in range(a['count']):
        o = base + k*stride
        out.append(struct.unpack('<'+fmt*n, g[o:o+sz*n]))
    return out

def m4_identity(): return [[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]
def m4_mul(A,B):
    return [[sum(A[i][k]*B[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def m4_from_trs(t,q,s):
    x,y,z,w = q
    xx,yy,zz = x*x,y*y,z*z
    xy,xz,yz = x*y,x*z,y*z
    wx,wy,wz = w*x,w*y,w*z
    R = [[1-2*(yy+zz), 2*(xy-wz),   2*(xz+wy)],
         [2*(xy+wz),   1-2*(xx+zz), 2*(yz-wx)],
         [2*(xz-wy),   2*(yz+wx),   1-2*(xx+yy)]]
    M = m4_identity()
    for i in range(3):
        for j in range(3):
            M[i][j] = R[i][j]*s[j]
        M[i][3] = t[i]
    return M
def node_matrix(nd):
    if 'matrix' in nd:
        m = nd['matrix']  # column-major
        return [[m[c*4+r] for c in range(4)] for r in range(4)]
    return m4_from_trs(nd.get('translation',[0,0,0]),
                       nd.get('rotation',[0,0,0,1]),
                       nd.get('scale',[1,1,1]))

def main(path):
    js, g = load_glb(path)
    print(f"### {path}")
    print(f"  generator: {(js.get('asset') or {}).get('generator')}")
    print(f"  scene: {js.get('scene')}  scenes={len(js.get('scenes',[]))}")
    print(f"  nodes={len(js.get('nodes',[]))} meshes={len(js.get('meshes',[]))} "
          f"skins={len(js.get('skins',[]))} materials={len(js.get('materials',[]))} "
          f"images={len(js.get('images',[]))} animations={len(js.get('animations',[]))}")
    if js.get('images'):
        for im in js['images']:
            print(f"    image: name={im.get('name')} mime={im.get('mimeType')} "
                  f"bufferView={im.get('bufferView')} uri={im.get('uri')}")
    if js.get('materials'):
        for mt in js['materials']:
            pbr = mt.get('pbrMetallicRoughness',{})
            print(f"    material: name={mt.get('name')} baseColorTex={'baseColorTexture' in pbr} "
                  f"baseColorFactor={pbr.get('baseColorFactor')}")
    # malhas + atributos + primitivas
    for mi, m in enumerate(js.get('meshes',[])):
        print(f"  mesh[{mi}] name={m.get('name')}")
        for pi, p in enumerate(m.get('primitives',[])):
            attrs = sorted(p['attributes'].keys())
            nv = js['accessors'][p['attributes']['POSITION']]['count']
            nt = js['accessors'][p['indices']]['count']//3 if 'indices' in p else 0
            print(f"    prim[{pi}] attrs={attrs} verts={nv} tris={nt} material={p.get('material')}")
    # skins
    for si, sk in enumerate(js.get('skins',[])):
        joints = sk.get('joints',[])
        names = [js['nodes'][j].get('name','?') for j in joints]
        print(f"  skin[{si}] name={sk.get('name')} joints={len(joints)} skeleton_root={sk.get('skeleton')}")
        print(f"    ossos: {names}")
    # bbox global (POSITION min/max de todas as primitivas)
    lo=[1e9]*3; hi=[-1e9]*3
    for m in js.get('meshes',[]):
        for p in m.get('primitives',[]):
            a = js['accessors'][p['attributes']['POSITION']]
            if 'min' in a and 'max' in a:
                for i in range(3):
                    lo[i]=min(lo[i],a['min'][i]); hi[i]=max(hi[i],a['max'][i])
    print(f"  bbox local: min=({lo[0]:.3f},{lo[1]:.3f},{lo[2]:.3f}) max=({hi[0]:.3f},{hi[1]:.3f},{hi[2]:.3f})")
    print(f"              tamanho=({hi[0]-lo[0]:.3f},{hi[1]-lo[1]:.3f},{hi[2]-lo[2]:.3f}) (unidades glTF = metros)")
    # hierarquia + posicoes de mundo dos ossos
    nodes = js.get('nodes',[])
    parent = {}
    for i,nd in enumerate(nodes):
        for c in nd.get('children',[]): parent[c]=i
    root = (js.get('scenes') or [{}])[js.get('scene',0) or 0].get('nodes',[0])
    world = {}
    def walk(i, pm):
        M = m4_mul(pm, node_matrix(nodes[i]))
        world[i] = M
        for c in nodes[i].get('children',[]): walk(c, M)
    for r in root: walk(r, m4_identity())
    print("  hierarquia de ossos (mundo; Y=up, metros):")
    joint_set = set()
    for sk in js.get('skins',[]): joint_set.update(sk.get('joints',[]))
    def show(i, d=0):
        if i not in world: return
        M = world[i]
        nm = nodes[i].get('name','?')
        mark = ' *osso' if i in joint_set else ''
        print(f"    {'  '*d}{nm}{mark} pos=({M[0][3]:+.3f},{M[1][3]:+.3f},{M[2][3]:+.3f})")
        for c in nodes[i].get('children',[]): show(c, d+1)
    for r in root: show(r)
    # superficies com skin (meshes ligados a skin)
    for i,nd in enumerate(nodes):
        if 'skin' in nd:
            print(f"  node '{nd.get('name')}' usa skin[{nd['skin']}] mesh={nd.get('mesh')}")

if __name__ == '__main__':
    main(sys.argv[1])
