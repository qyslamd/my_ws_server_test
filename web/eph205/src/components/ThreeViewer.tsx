// 系统三维连接示意：加载 help3d/system_3d.json + OBJ/MTL。
//
// 资产沿用参考项目的导出约定：OBJ 是 Blender 的 Z-up、顶点在局部坐标，
// JSON 的 position 是 Y-up 的世界坐标（Y 为高度）。因此每个部件先绕 X
// 轴 -90° 把 Z-up 转成 Y-up，再平移到 position。

import { useEffect, useRef, useState } from 'react'
import { Button, Space, Tag } from 'antd'
import * as THREE from 'three'
import { OBJLoader } from 'three/addons/loaders/OBJLoader.js'
import { MTLLoader } from 'three/addons/loaders/MTLLoader.js'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'

interface ScenePart {
  file: string
  label: string
  position: [number, number, number]
}

interface SceneDesc {
  name: string
  parts: ScenePart[]
}

const HIGHLIGHT = new THREE.Color(0x2f6bff)

export default function ThreeViewer() {
  const hostRef = useRef<HTMLDivElement>(null)
  const [parts, setParts] = useState<ScenePart[]>([])
  const [picked, setPicked] = useState<string>('')
  const [status, setStatus] = useState('加载中…')
  const [failed, setFailed] = useState(false)
  const resetRef = useRef<(() => void) | null>(null)

  useEffect(() => {
    const host = hostRef.current
    if (!host) {
      return
    }
    const base = new URL('help3d/', document.baseURI).href

    const renderer = new THREE.WebGLRenderer({ antialias: true })
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2))
    renderer.setSize(host.clientWidth || 800, host.clientHeight || 460)
    renderer.setClearColor(0x1b1f2a, 1)
    host.appendChild(renderer.domElement)

    const scene = new THREE.Scene()
    scene.background = new THREE.Color(0x1b1f2a)

    const camera = new THREE.PerspectiveCamera(
      45,
      (host.clientWidth || 800) / (host.clientHeight || 460),
      0.01,
      200,
    )
    camera.position.set(1.8, 1.7, 2.2)

    const controls = new OrbitControls(camera, renderer.domElement)
    controls.enableDamping = true
    controls.dampingFactor = 0.08
    controls.target.set(0, 0.5, 0)

    scene.add(new THREE.AmbientLight(0xffffff, 0.9))
    scene.add(new THREE.HemisphereLight(0xffffff, 0x5a6376, 2.6))
    const key = new THREE.DirectionalLight(0xffffff, 2.4)
    key.position.set(2.5, 4, 3)
    scene.add(key)
    const fill = new THREE.DirectionalLight(0xffffff, 1.1)
    fill.position.set(-3, 2, -2)
    scene.add(fill)

    const root = new THREE.Group()
    scene.add(root)

    let disposed = false
    let raf = 0
    const clock = new THREE.Clock()

    const resize = () => {
      const w = host.clientWidth || 800
      const h = host.clientHeight || 460
      renderer.setSize(w, h)
      camera.aspect = w / h
      camera.updateProjectionMatrix()
    }
    const observer = new ResizeObserver(resize)
    observer.observe(host)

    const animate = () => {
      raf = requestAnimationFrame(animate)
      const t = clock.getElapsedTime()
      // 轻微呼吸光，让静态模型不至于死板
      key.intensity = 1.9 + Math.sin(t * 0.8) * 0.15
      controls.update()
      renderer.render(scene, camera)
    }

    const disposeObject = (obj: THREE.Object3D) => {
      obj.traverse((child) => {
        const mesh = child as THREE.Mesh
        if (mesh.geometry) {
          mesh.geometry.dispose()
        }
        const material = mesh.material as THREE.Material | THREE.Material[]
        if (Array.isArray(material)) {
          material.forEach((m) => m.dispose())
        } else if (material) {
          material.dispose()
        }
      })
    }

    // 同一份 OBJ 可能被放置多次（三块多路器板），先加载一次再克隆
    const templates = new Map<string, THREE.Group>()
    const loadPart = (file: string): Promise<THREE.Group> => {
      const cached = templates.get(file)
      if (cached) {
        return Promise.resolve(cached)
      }
      const slash = file.lastIndexOf('/')
      const dir = file.slice(0, slash + 1)
      const name = file.slice(slash + 1)
      const mtl = name.replace(/\.obj$/i, '.mtl')
      return new Promise((resolve, reject) => {
        new MTLLoader()
          .setPath(base + dir)
          .load(
            mtl,
            (materials) => {
              materials.preload()
              new OBJLoader()
                .setPath(base + dir)
                .setMaterials(materials)
                .load(
                  name,
                  (obj) => {
                    templates.set(file, obj)
                    resolve(obj)
                  },
                  undefined,
                  () => reject(new Error('无法加载 ' + name)),
                )
            },
            undefined,
            () => {
              // 没有 MTL 就退化成默认材质，至少把模型显示出来
              new OBJLoader().setPath(base + dir).load(
                name,
                (obj) => {
                  templates.set(file, obj)
                  resolve(obj)
                },
                undefined,
                () => reject(new Error('无法加载 ' + name)),
              )
            },
          )
      })
    }

    const raycaster = new THREE.Raycaster()
    const pointer = new THREE.Vector2()
    let highlighted: THREE.Mesh[] = []

    const clearHighlight = () => {
      highlighted.forEach((mesh) => {
        const mat = mesh.material as THREE.MeshStandardMaterial
        if (mat && mat.userData.originalEmissive !== undefined) {
          mat.emissive.setHex(mat.userData.originalEmissive)
          mat.emissiveIntensity = mat.userData.originalIntensity ?? 1
        }
      })
      highlighted = []
    }

    const onPointerDown = (event: PointerEvent) => {
      const rect = renderer.domElement.getBoundingClientRect()
      pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1
      pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1
      raycaster.setFromCamera(pointer, camera)
      const hits = raycaster.intersectObjects(root.children, true)
      clearHighlight()
      if (hits.length === 0) {
        setPicked('')
        return
      }
      let node: THREE.Object3D | null = hits[0].object
      while (node && !node.userData.label) {
        node = node.parent
      }
      if (!node) {
        setPicked('')
        return
      }
      setPicked(String(node.userData.label))
      node.traverse((child) => {
        const mesh = child as THREE.Mesh
        const mat = mesh.material as THREE.MeshStandardMaterial
        if (!mat || !(mat as any).emissive) {
          return
        }
        if (mat.userData.originalEmissive === undefined) {
          mat.userData.originalEmissive = mat.emissive.getHex()
          mat.userData.originalIntensity = mat.emissiveIntensity
        }
        mat.emissive.copy(HIGHLIGHT)
        mat.emissiveIntensity = 0.55
        highlighted.push(mesh)
      })
    }
    renderer.domElement.addEventListener('pointerdown', onPointerDown)

    const frameScene = () => {
      const box = new THREE.Box3().setFromObject(root)
      if (box.isEmpty()) {
        return
      }
      const center = box.getCenter(new THREE.Vector3())
      const size = box.getSize(new THREE.Vector3())
      const radius = Math.max(size.length() * 0.5, 0.5)
      // 按包围球半径取距离，方向归一化后乘过去，避免斜向多退一截
      const distance = (radius / Math.tan((camera.fov * Math.PI) / 360)) * 0.95
      const dir = new THREE.Vector3(0.75, 0.55, 0.85).normalize()
      controls.target.copy(center)
      camera.position.copy(center).addScaledVector(dir, distance)
      camera.near = Math.max(radius * 0.01, 0.001)
      camera.far = radius * 50
      camera.updateProjectionMatrix()
      controls.minDistance = radius * 0.4
      controls.maxDistance = radius * 6
      controls.update()
    }
    resetRef.current = frameScene

    fetch(base + 'system_3d.json')
      .then((resp) => {
        if (!resp.ok) {
          throw new Error('HTTP ' + resp.status)
        }
        return resp.json() as Promise<SceneDesc>
      })
      .then(async (desc) => {
        if (disposed) {
          return
        }
        setParts(desc.parts ?? [])
        for (const part of desc.parts ?? []) {
          const template = await loadPart(part.file)
          if (disposed) {
            return
          }
          const instance = template.clone(true)
          // 克隆时材质默认共享，改一个会串色，这里逐个复制
          instance.traverse((child) => {
            const mesh = child as THREE.Mesh
            if (mesh.material) {
              mesh.material = Array.isArray(mesh.material)
                ? mesh.material.map((m) => m.clone())
                : (mesh.material as THREE.Material).clone()
            }
          })
          instance.rotation.x = -Math.PI / 2
          const [x, y, z] = part.position ?? [0, 0, 0]
          instance.position.set(x, y, z)
          instance.userData.label = part.label
          instance.name = part.label
          root.add(instance)
        }
        frameScene()
        setStatus(`已加载 ${desc.parts?.length ?? 0} 个部件`)
        setFailed(false)
      })
      .catch((err: Error) => {
        if (disposed) {
          return
        }
        setStatus('三维资源加载失败：' + err.message)
        setFailed(true)
      })

    animate()

    return () => {
      disposed = true
      cancelAnimationFrame(raf)
      observer.disconnect()
      renderer.domElement.removeEventListener('pointerdown', onPointerDown)
      resetRef.current = null
      controls.dispose()
      clearHighlight()
      disposeObject(root)
      templates.clear()
      renderer.dispose()
      host.removeChild(renderer.domElement)
    }
  }, [])

  return (
    <div>
      <div className="toolbar" style={{ marginBottom: 8 }}>
        <Button size="small" onClick={() => resetRef.current?.()}>
          复位视角
        </Button>
        <span style={{ color: '#667085', fontSize: 12 }}>
          左键旋转 · 滚轮缩放 · 右键平移 · 点击部件查看名称
        </span>
        <span style={{ flex: 1 }} />
        {picked && <Tag color="blue">{picked}</Tag>}
        <Tag color={failed ? 'red' : 'default'}>{status}</Tag>
      </div>
      <div
        ref={hostRef}
        style={{
          width: '100%',
          height: 460,
          borderRadius: 6,
          overflow: 'hidden',
          background: '#1b1f2a',
        }}
      />
      {parts.length > 0 && (
        <Space size={[6, 6]} wrap style={{ marginTop: 8 }}>
          {parts.map((part) => (
            <Tag
              key={part.label}
              color={picked === part.label ? 'blue' : undefined}
            >
              {part.label}
            </Tag>
          ))}
        </Space>
      )}
    </div>
  )
}
