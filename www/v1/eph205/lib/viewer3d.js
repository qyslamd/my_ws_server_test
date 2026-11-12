// 系统三维连接示意：加载 help3d/system_3d.json + OBJ/MTL。
//
// 资产沿用参考项目的导出约定：OBJ 是 Blender 的 Z-up、顶点在局部坐标，
// JSON 的 position 是 Y-up 世界坐标（Y 为高度）。所以每个部件先绕 X 轴
// -90° 转成 Y-up，再平移到 position。

import * as THREE from 'three';
import { OBJLoader } from 'three/addons/loaders/OBJLoader.js';
import { MTLLoader } from 'three/addons/loaders/MTLLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { el, clear } from './ui.js';

const HIGHLIGHT = new THREE.Color(0x2f6bff);

export function createViewer3d() {
  const base = new URL('../help3d/', import.meta.url).href;
  const host = el('div', { class: 'viewer-host' });
  const statusTag = el('span', { class: 'tag muted', text: '加载中…' });
  const pickedTag = el('span', { class: 'tag info', style: { display: 'none' } });
  const partList = el('div', { class: 'row', style: { marginTop: '8px' } });
  const btnReset = el('button', { class: 'secondary', text: '复位视角' });

  const renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.setClearColor(0x1b1f2a, 1);
  host.appendChild(renderer.domElement);

  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x1b1f2a);
  const camera = new THREE.PerspectiveCamera(45, 1, 0.01, 200);
  camera.position.set(1.8, 1.7, 2.2);
  const controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.dampingFactor = 0.08;
  controls.target.set(0, 0.5, 0);

  scene.add(new THREE.AmbientLight(0xffffff, 0.9));
  scene.add(new THREE.HemisphereLight(0xffffff, 0x5a6376, 2.6));
  const key = new THREE.DirectionalLight(0xffffff, 2.4);
  key.position.set(2.5, 4, 3);
  scene.add(key);
  const fill = new THREE.DirectionalLight(0xffffff, 1.1);
  fill.position.set(-3, 2, -2);
  scene.add(fill);

  const root = new THREE.Group();
  scene.add(root);

  let raf = 0;
  let disposed = false;
  let highlighted = [];
  const clock = new THREE.Clock();

  function resize() {
    const w = host.clientWidth || 800;
    const h = host.clientHeight || 460;
    renderer.setSize(w, h);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  }
  const observer = new ResizeObserver(resize);
  observer.observe(host);

  function animate() {
    raf = requestAnimationFrame(animate);
    const t = clock.getElapsedTime();
    key.intensity = 1.9 + Math.sin(t * 0.8) * 0.15;
    controls.update();
    renderer.render(scene, camera);
  }

  function frameScene() {
    const box = new THREE.Box3().setFromObject(root);
    if (box.isEmpty()) return;
    const center = box.getCenter(new THREE.Vector3());
    const size = box.getSize(new THREE.Vector3());
    const radius = Math.max(size.length() * 0.5, 0.5);
    const distance = (radius / Math.tan((camera.fov * Math.PI) / 360)) * 0.95;
    const dir = new THREE.Vector3(0.75, 0.55, 0.85).normalize();
    controls.target.copy(center);
    camera.position.copy(center).addScaledVector(dir, distance);
    camera.near = Math.max(radius * 0.01, 0.001);
    camera.far = radius * 50;
    camera.updateProjectionMatrix();
    controls.minDistance = radius * 0.4;
    controls.maxDistance = radius * 6;
    controls.update();
  }
  btnReset.addEventListener('click', frameScene);

  function clearHighlight() {
    for (const mesh of highlighted) {
      const mat = mesh.material;
      if (mat && mat.userData.originalEmissive !== undefined) {
        mat.emissive.setHex(mat.userData.originalEmissive);
        mat.emissiveIntensity = mat.userData.originalIntensity ?? 1;
      }
    }
    highlighted = [];
  }

  const raycaster = new THREE.Raycaster();
  const pointer = new THREE.Vector2();
  renderer.domElement.addEventListener('pointerdown', (event) => {
    const rect = renderer.domElement.getBoundingClientRect();
    pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
    pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
    raycaster.setFromCamera(pointer, camera);
    const hits = raycaster.intersectObjects(root.children, true);
    clearHighlight();
    if (hits.length === 0) {
      pickedTag.style.display = 'none';
      return;
    }
    let node = hits[0].object;
    while (node && !node.userData.label) node = node.parent;
    if (!node) {
      pickedTag.style.display = 'none';
      return;
    }
    pickedTag.style.display = '';
    pickedTag.textContent = node.userData.label;
    node.traverse((child) => {
      const mat = child.material;
      if (!mat || !mat.emissive) return;
      if (mat.userData.originalEmissive === undefined) {
        mat.userData.originalEmissive = mat.emissive.getHex();
        mat.userData.originalIntensity = mat.emissiveIntensity;
      }
      mat.emissive.copy(HIGHLIGHT);
      mat.emissiveIntensity = 0.55;
      highlighted.push(child);
    });
  });

  const templates = new Map();

  function loadPart(file) {
    if (templates.has(file)) return Promise.resolve(templates.get(file));
    const slash = file.lastIndexOf('/');
    const dir = file.slice(0, slash + 1);
    const name = file.slice(slash + 1);
    const mtl = name.replace(/\.obj$/i, '.mtl');
    return new Promise((resolve, reject) => {
      new MTLLoader()
        .setPath(base + dir)
        .load(
          mtl,
          (materials) => {
            materials.preload();
            new OBJLoader()
              .setPath(base + dir)
              .setMaterials(materials)
              .load(
                name,
                (obj) => {
                  templates.set(file, obj);
                  resolve(obj);
                },
                undefined,
                () => reject(new Error('无法加载 ' + name)),
              );
          },
          undefined,
          () => {
            new OBJLoader().setPath(base + dir).load(
              name,
              (obj) => {
                templates.set(file, obj);
                resolve(obj);
              },
              undefined,
              () => reject(new Error('无法加载 ' + name)),
            );
          },
        );
    });
  }

  (async () => {
    try {
      const resp = await fetch(base + 'system_3d.json');
      if (!resp.ok) throw new Error('HTTP ' + resp.status);
      const desc = await resp.json();
      const parts = desc.parts || [];
      for (const part of parts) {
        const template = await loadPart(part.file);
        if (disposed) return;
        const instance = template.clone(true);
        // 克隆时材质默认共享，改一个会串色
        instance.traverse((child) => {
          if (child.material) {
            child.material = Array.isArray(child.material)
              ? child.material.map((m) => m.clone())
              : child.material.clone();
          }
        });
        instance.rotation.x = -Math.PI / 2;
        const [x, y, z] = part.position || [0, 0, 0];
        instance.position.set(x, y, z);
        instance.userData.label = part.label;
        root.add(instance);
      }
      frameScene();
      statusTag.className = 'tag';
      statusTag.textContent = `已加载 ${parts.length} 个部件`;
      clear(partList);
      for (const part of parts) {
        partList.appendChild(el('span', { class: 'tag', text: part.label }));
      }
      resize();
    } catch (err) {
      statusTag.className = 'tag fail';
      statusTag.textContent = '三维资源加载失败：' + err.message;
    }
  })();

  resize();
  animate();

  const elRoot = el(
    'div',
    {},
    el(
      'div',
      { class: 'row', style: { marginBottom: '8px' } },
      btnReset,
      el('span', { class: 'field', text: '左键旋转 · 滚轮缩放 · 右键平移 · 点击部件查看名称' }),
      el('span', { class: 'spacer' }),
      pickedTag,
      statusTag,
    ),
    host,
    partList,
  );

  return {
    el: elRoot,
    dispose() {
      disposed = true;
      cancelAnimationFrame(raf);
      observer.disconnect();
      controls.dispose();
      clearHighlight();
      root.traverse((child) => {
        if (child.geometry) child.geometry.dispose();
        if (child.material) {
          const list = Array.isArray(child.material) ? child.material : [child.material];
          list.forEach((m) => m.dispose());
        }
      });
      templates.clear();
      renderer.dispose();
    },
  };
}
