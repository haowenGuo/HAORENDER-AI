import { useEffect, useRef, useState } from "react";
import * as THREE from "three";
import { GLTFLoader } from "three/examples/jsm/loaders/GLTFLoader.js";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";

type ThreePreviewProps = {
  fileUrl?: string;
  fallbackImageUrl?: string;
  label: string;
};

function isGltfLike(url?: string) {
  if (!url) return false;
  const clean = url.split("?")[0].toLowerCase();
  return clean.endsWith(".glb") || clean.endsWith(".gltf") || clean.endsWith(".vrm");
}

export default function ThreePreview({ fileUrl, fallbackImageUrl, label }: ThreePreviewProps) {
  const mountRef = useRef<HTMLDivElement | null>(null);
  const [message, setMessage] = useState("");

  useEffect(() => {
    const mount = mountRef.current;
    if (!mount || !isGltfLike(fileUrl)) {
      return;
    }

    setMessage("正在加载 Three.js 预览...");
    const scene = new THREE.Scene();
    scene.background = new THREE.Color("#121411");
    const camera = new THREE.PerspectiveCamera(45, mount.clientWidth / mount.clientHeight, 0.01, 5000);
    camera.position.set(2.8, 1.8, 3.8);

    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setSize(mount.clientWidth, mount.clientHeight);
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    mount.appendChild(renderer.domElement);

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;

    const key = new THREE.DirectionalLight("#ffffff", 2.2);
    key.position.set(3, 4, 5);
    scene.add(key);
    const fill = new THREE.DirectionalLight("#8fb8ff", 0.75);
    fill.position.set(-4, 2, 2);
    scene.add(fill);
    scene.add(new THREE.AmbientLight("#f0ead8", 0.72));

    const grid = new THREE.GridHelper(6, 12, "#41483d", "#242820");
    grid.position.y = -0.01;
    scene.add(grid);

    let disposed = false;
    let model: THREE.Object3D | null = null;
    const loader = new GLTFLoader();
    loader.load(
      fileUrl!,
      (gltf) => {
        if (disposed) return;
        model = gltf.scene;
        scene.add(model);
        const box = new THREE.Box3().setFromObject(model);
        const size = box.getSize(new THREE.Vector3());
        const center = box.getCenter(new THREE.Vector3());
        const maxAxis = Math.max(size.x, size.y, size.z, 0.001);
        model.position.sub(center);
        const scale = 2.6 / maxAxis;
        model.scale.setScalar(scale);
        controls.target.set(0, 0.4, 0);
        camera.position.set(2.8, 1.7, 3.6);
        controls.update();
        setMessage("");
      },
      undefined,
      (error) => {
        console.error(error);
        setMessage("Three.js 无法直接预览该文件，已切换到 C++ 缩略图。");
      }
    );

    const resizeObserver = new ResizeObserver(() => {
      if (!mount.clientWidth || !mount.clientHeight) return;
      camera.aspect = mount.clientWidth / mount.clientHeight;
      camera.updateProjectionMatrix();
      renderer.setSize(mount.clientWidth, mount.clientHeight);
    });
    resizeObserver.observe(mount);

    let frame = 0;
    const animate = () => {
      frame = requestAnimationFrame(animate);
      controls.update();
      renderer.render(scene, camera);
    };
    animate();

    return () => {
      disposed = true;
      cancelAnimationFrame(frame);
      resizeObserver.disconnect();
      controls.dispose();
      renderer.dispose();
      if (model) {
        model.traverse((child) => {
          const mesh = child as THREE.Mesh;
          mesh.geometry?.dispose?.();
          const material = mesh.material as THREE.Material | THREE.Material[] | undefined;
          if (Array.isArray(material)) {
            material.forEach((item) => item.dispose());
          } else {
            material?.dispose?.();
          }
        });
      }
      mount.removeChild(renderer.domElement);
    };
  }, [fileUrl]);

  const showFallback = !isGltfLike(fileUrl) || Boolean(message && fallbackImageUrl);

  return (
    <div className="preview-frame">
      <div className="preview-label">
        {label}
      </div>
      <div ref={mountRef} className={showFallback ? "hidden h-full" : "h-full"} />
      {showFallback && fallbackImageUrl ? (
        <img src={fallbackImageUrl} className="preview-fallback-image" />
      ) : null}
      {(message || (!fileUrl && !fallbackImageUrl)) && !fallbackImageUrl && (
        <div className="preview-art">
          <div className="preview-art-inner">
            <div className="preview-stage" />
            <div className="preview-object" />
            <div className="preview-side" />
            <div className="preview-caption">{message || "未加载模型"}</div>
          </div>
        </div>
      )}
    </div>
  );
}
