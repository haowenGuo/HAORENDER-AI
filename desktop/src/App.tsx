import { useMemo, useState } from "react";
import type { ComponentType, ReactNode } from "react";
import {
  ArrowRight,
  Box,
  CheckCircle2,
  ChevronRight,
  Download,
  FileSearch,
  FolderOpen,
  Layers3,
  Loader2,
  MonitorPlay,
  Play,
  RefreshCcw,
  Share2,
  Sparkles,
  Triangle,
  Wand2
} from "lucide-react";
import ThreePreview from "./components/ThreePreview";
import type { CoreConvertResult, CoreInspectResult, CorePreviewResult } from "./types";

const targetFormats = ["glb", "gltf", "fbx", "obj", "dae", "stl", "ply", "vrm"];

function formatNumber(value?: number) {
  return new Intl.NumberFormat("zh-CN").format(value ?? 0);
}

function fileName(path?: string) {
  if (!path) return "未选择文件";
  return path.split(/[\\/]/).pop() ?? path;
}

function fileExtension(path?: string) {
  if (!path) return "";
  return path.split(".").pop()?.toUpperCase() ?? "";
}

function isPreviewableInThree(path?: string) {
  const ext = fileExtension(path).toLowerCase();
  return ext === "glb" || ext === "gltf" || ext === "vrm";
}

function generatePlan(inspect: CoreInspectResult | null, targetFormat: string) {
  if (!inspect?.ok) {
    return [
      "导入模型后，系统会先读取网格、材质、骨骼、morph 和动画概况。",
      "然后根据目标格式生成转换计划，并在执行后重新读取导出文件做对比。"
    ];
  }
  const summary = inspect.summary ?? {};
  const steps = [
    `读取源文件 ${fileName(inspect.source)}，当前格式为 ${inspect.format ?? "未知"}。`,
    `统计 ${formatNumber(summary.meshes)} 个网格、${formatNumber(summary.triangles)} 个三角形、${formatNumber(summary.textures)} 张贴图。`,
    `检查 ${formatNumber(summary.skeletonBones)} 根骨骼、${formatNumber(summary.morphTargets)} 个 morph、${formatNumber(summary.animations)} 个动画片段。`,
    `转换为 ${targetFormat.toUpperCase()}，保留模型层级、材质参数、骨骼和动画数据。`,
    "导出后重新 inspect，并把转换前后放到同一个工作台中对比。"
  ];
  if (targetFormat === "vrm") {
    steps.splice(4, 0, "VRM 导出会写入 VRM 1.0 元信息和 humanoid 骨骼映射；如果人形骨骼识别不足，会在报告里标记风险。");
  }
  if (targetFormat === "obj" || targetFormat === "stl" || targetFormat === "ply") {
    steps.splice(4, 0, "目标格式偏几何数据，骨骼、动画、morph 和部分材质信息可能无法完整保留。");
  }
  return steps;
}

function Metric({ label, value, icon: Icon }: { label: string; value?: number; icon: typeof Box }) {
  return (
    <div className="rounded-lg border border-app-line bg-app-panel2 p-3">
      <div className="flex items-center gap-2 text-xs text-slate-400">
        <Icon className="h-3.5 w-3.5" />
        {label}
      </div>
      <div className="mt-2 text-xl font-semibold tracking-normal text-app-text">{formatNumber(value)}</div>
    </div>
  );
}

function Button({
  children,
  onClick,
  disabled,
  tone = "default"
}: {
  children: React.ReactNode;
  onClick?: () => void;
  disabled?: boolean;
  tone?: "default" | "primary" | "quiet";
}) {
  const toneClass =
    tone === "primary"
      ? "border-blue-400/40 bg-blue-500 text-white hover:bg-blue-400"
      : tone === "quiet"
        ? "border-transparent bg-transparent text-slate-300 hover:bg-white/5"
        : "border-app-line bg-app-panel2 text-slate-100 hover:border-blue-400/60 hover:bg-[#1b2634]";
  return (
    <button
      className={`no-drag inline-flex h-9 items-center justify-center gap-2 rounded-lg border px-3 text-sm font-medium transition disabled:cursor-not-allowed disabled:opacity-45 ${toneClass}`}
      disabled={disabled}
      onClick={onClick}
    >
      {children}
    </button>
  );
}

function Panel({ title, children, action }: { title: string; children: React.ReactNode; action?: React.ReactNode }) {
  return (
    <section className="rounded-lg border border-app-line bg-app-panel shadow-soft">
      <div className="flex h-12 items-center justify-between border-b border-app-line px-4">
        <h2 className="text-sm font-semibold text-slate-100">{title}</h2>
        {action}
      </div>
      <div className="p-4">{children}</div>
    </section>
  );
}

export default function App() {
  const [sourcePath, setSourcePath] = useState("");
  const [targetFormat, setTargetFormat] = useState("glb");
  const [outputPath, setOutputPath] = useState("");
  const [inspect, setInspect] = useState<CoreInspectResult | null>(null);
  const [sourcePreview, setSourcePreview] = useState<CorePreviewResult | null>(null);
  const [convertResult, setConvertResult] = useState<CoreConvertResult | null>(null);
  const [convertedPreview, setConvertedPreview] = useState<CorePreviewResult | null>(null);
  const [busy, setBusy] = useState("");
  const [logs, setLogs] = useState<string[]>(["HAORENDER-AI Desktop 已就绪。"]);

  const plan = useMemo(() => generatePlan(inspect, targetFormat), [inspect, targetFormat]);
  const summary = inspect?.summary ?? {};
  const outputModelPath = convertResult?.outputPath || convertResult?.output || outputPath;

  function pushLog(message: string) {
    setLogs((items) => [`${new Date().toLocaleTimeString("zh-CN")}  ${message}`, ...items].slice(0, 80));
  }

  async function launchWorkbench(kind: "render" | "rig" | "asset") {
    const names = {
      render: "渲染预览",
      rig: "骨骼映射",
      asset: "资产转换"
    };
    pushLog(`${names[kind]}已经迁移到 Electron 内部页面。`);
  }

  async function inspectAndPreview(path: string) {
    setBusy("inspect");
    pushLog(`开始分析：${path}`);
    try {
      const result = await window.haoRender.inspectModel(path);
      setInspect(result);
      pushLog(result.ok ? "模型概况读取完成。" : `模型读取失败：${result.error ?? result.stderr ?? "未知错误"}`);
      const preview = await window.haoRender.renderPreview(path);
      setSourcePreview(preview);
      if (preview.ok) pushLog("C++ 缩略图已生成。");
    } finally {
      setBusy("");
    }
  }

  async function openModel() {
    const path = await window.haoRender.openModel();
    if (!path) return;
    setSourcePath(path);
    setOutputPath("");
    setConvertResult(null);
    setConvertedPreview(null);
    await inspectAndPreview(path);
  }

  async function chooseOutput() {
    if (!sourcePath) return;
    const path = await window.haoRender.chooseOutput(sourcePath, targetFormat);
    if (path) setOutputPath(path);
  }

  async function convertModel() {
    if (!sourcePath) return;
    setBusy("convert");
    pushLog(`开始转换为 ${targetFormat.toUpperCase()}。`);
    try {
      const result = await window.haoRender.convertModel({ sourcePath, targetFormat, outputPath });
      setConvertResult(result);
      if (result.ok) {
        pushLog(`转换完成：${result.outputPath ?? result.output}`);
        const previewTarget = result.outputPath ?? result.output;
        if (previewTarget) {
          const preview = await window.haoRender.renderPreview(previewTarget);
          setConvertedPreview(preview);
          if (preview.ok) pushLog("转换后预览已生成。");
        }
      } else {
        pushLog(`转换失败：${result.status ?? result.error ?? result.stderr ?? "未知错误"}`);
      }
    } finally {
      setBusy("");
    }
  }

  return (
    <div className="flex h-screen bg-app-bg text-app-text">
      <aside className="flex w-[276px] shrink-0 flex-col border-r border-app-line bg-[#0b1017]">
        <div className="drag-region flex h-20 items-center px-5">
          <div>
            <div className="flex items-center gap-2 text-lg font-semibold">
              <Sparkles className="h-5 w-5 text-app-accent2" />
              HAORENDER-AI
            </div>
            <div className="mt-1 text-xs text-slate-500">GPU Rendering + AI Pipeline</div>
          </div>
        </div>
        <nav className="space-y-1 px-3">
          {[
            { label: "模型转换", active: true },
            { label: "渲染工作台", onClick: () => launchWorkbench("render") },
            { label: "骨骼映射", onClick: () => launchWorkbench("rig") },
            { label: "资源检查" },
            { label: "前后对比" },
            { label: "转换报告" }
          ].map((item) => (
            <button
              key={item.label}
              className={`no-drag flex h-10 w-full items-center gap-3 rounded-lg px-3 text-left text-sm transition ${
                item.active ? "bg-blue-500/15 text-blue-200" : "text-slate-400 hover:bg-white/5 hover:text-slate-200"
              }`}
              onClick={item.onClick}
            >
              <ChevronRight className="h-4 w-4" />
              {item.label}
            </button>
          ))}
        </nav>
        <div className="mt-auto border-t border-app-line p-4 text-xs leading-6 text-slate-500">
          <div>后台核心</div>
          <div className="truncate text-slate-300">{convertResult?.corePath ?? inspect?.corePath ?? "haorender-core.exe"}</div>
        </div>
      </aside>

      <main className="flex min-w-0 flex-1 flex-col">
        <header className="drag-region flex h-20 items-center justify-between border-b border-app-line bg-app-bg/95 px-6">
          <div>
            <h1 className="text-xl font-semibold tracking-normal">模型格式转换工作台</h1>
            <p className="mt-1 text-sm text-slate-400">导入模型，生成转换计划，执行转换，并对比转换前后的资源质量。</p>
          </div>
          <div className="no-drag flex items-center gap-2">
            <Button onClick={() => launchWorkbench("render")}>
              <MonitorPlay className="h-4 w-4" />
              渲染工作台
            </Button>
            <Button onClick={() => launchWorkbench("rig")}>
              <Share2 className="h-4 w-4" />
              骨骼映射
            </Button>
            <Button onClick={openModel} disabled={Boolean(busy)}>
              <FolderOpen className="h-4 w-4" />
              导入模型
            </Button>
            <Button onClick={() => sourcePath && inspectAndPreview(sourcePath)} disabled={!sourcePath || Boolean(busy)}>
              <RefreshCcw className="h-4 w-4" />
              重新分析
            </Button>
            <Button tone="primary" onClick={convertModel} disabled={!sourcePath || Boolean(busy)}>
              {busy === "convert" ? <Loader2 className="h-4 w-4 animate-spin" /> : <Play className="h-4 w-4" />}
              执行转换
            </Button>
          </div>
        </header>

        <div className="grid min-h-0 flex-1 grid-cols-[minmax(0,1fr)_380px] gap-4 p-4">
          <section className="flex min-w-0 flex-col gap-4">
            <div className="grid min-h-[390px] flex-1 grid-cols-2 gap-4">
              <ThreePreview
                label={`转换前 ${fileExtension(sourcePath) || ""}`}
                fileUrl={isPreviewableInThree(sourcePath) ? inspect?.fileUrl : undefined}
                fallbackImageUrl={sourcePreview?.previewUrl}
              />
              <ThreePreview
                label={`转换后 ${targetFormat.toUpperCase()}`}
                fileUrl={isPreviewableInThree(outputModelPath) ? convertResult?.outputFileUrl : undefined}
                fallbackImageUrl={convertedPreview?.previewUrl}
              />
            </div>

            <div className="grid grid-cols-4 gap-3">
              <Metric label="网格" value={summary.meshes} icon={Box} />
              <Metric label="三角形" value={summary.triangles} icon={Triangle} />
              <Metric label="骨骼" value={summary.skeletonBones} icon={Layers3} />
              <Metric label="动画" value={summary.animations} icon={Play} />
            </div>

            <Panel
              title="执行日志"
              action={
                outputModelPath ? (
                  <Button tone="quiet" onClick={() => window.haoRender.showItemInFolder(outputModelPath)}>
                    <Download className="h-4 w-4" />
                    定位输出
                  </Button>
                ) : null
              }
            >
              <div className="scrollbar-soft h-28 overflow-auto rounded-lg bg-[#0b1118] p-3 font-mono text-xs leading-6 text-slate-300">
                {logs.map((log) => (
                  <div key={log}>{log}</div>
                ))}
              </div>
            </Panel>
          </section>

          <aside className="scrollbar-soft flex min-h-0 flex-col gap-4 overflow-auto pr-1">
            <Panel title="保留的 C++ 工作台">
              <div className="space-y-3 text-sm text-slate-300">
                <p className="leading-6 text-slate-400">
                  Electron 是新的产品外壳；渲染和 Rig 还没有完全迁成原生前端页。迁移期间先直接打开成熟的 C++ 工作台，避免功能断档。
                </p>
                <div className="grid grid-cols-2 gap-2">
                  <Button onClick={() => launchWorkbench("render")}>
                    <MonitorPlay className="h-4 w-4" />
                    渲染
                  </Button>
                  <Button onClick={() => launchWorkbench("rig")}>
                    <Share2 className="h-4 w-4" />
                    Rig AI
                  </Button>
                </div>
              </div>
            </Panel>

            <Panel title="转换设置">
              <div className="space-y-4">
                <div>
                  <div className="mb-2 text-xs font-medium text-slate-400">源文件</div>
                  <div className="rounded-lg border border-app-line bg-[#0b1118] px-3 py-2 text-sm text-slate-200">
                    <div className="truncate">{fileName(sourcePath)}</div>
                    <div className="mt-1 truncate text-xs text-slate-500">{sourcePath || "请选择一个模型文件"}</div>
                  </div>
                </div>
                <div>
                  <div className="mb-2 text-xs font-medium text-slate-400">目标格式</div>
                  <select
                    className="h-10 w-full rounded-lg border border-app-line bg-app-panel2 px-3 text-sm text-slate-100 outline-none focus:border-blue-400"
                    value={targetFormat}
                    onChange={(event) => {
                      setTargetFormat(event.target.value);
                      setOutputPath("");
                      setConvertResult(null);
                      setConvertedPreview(null);
                    }}
                  >
                    {targetFormats.map((format) => (
                      <option key={format} value={format}>
                        {format.toUpperCase()}
                      </option>
                    ))}
                  </select>
                </div>
                <div>
                  <div className="mb-2 text-xs font-medium text-slate-400">输出文件</div>
                  <div className="flex gap-2">
                    <div className="min-w-0 flex-1 rounded-lg border border-app-line bg-[#0b1118] px-3 py-2 text-sm text-slate-300">
                      <div className="truncate">{outputPath || "默认输出到源文件旁边的 HAORENDER-AI-Converted"}</div>
                    </div>
                    <Button onClick={chooseOutput} disabled={!sourcePath}>
                      选择
                    </Button>
                  </div>
                </div>
              </div>
            </Panel>

            <Panel title="AI 转换计划" action={<Wand2 className="h-4 w-4 text-app-accent2" />}>
              <ol className="space-y-3">
                {plan.map((step, index) => (
                  <li key={step} className="flex gap-3 text-sm leading-6 text-slate-300">
                    <span className="mt-0.5 flex h-6 w-6 shrink-0 items-center justify-center rounded-full bg-blue-500/15 text-xs font-semibold text-blue-200">
                      {index + 1}
                    </span>
                    <span>{step}</span>
                  </li>
                ))}
              </ol>
            </Panel>

            <Panel title="资源概况" action={inspect?.ok ? <CheckCircle2 className="h-4 w-4 text-app-accent2" /> : <FileSearch className="h-4 w-4 text-slate-500" />}>
              {inspect?.ok ? (
                <div className="space-y-4">
                  <div className="flex flex-wrap gap-2">
                    {(inspect.materialTags ?? []).map((tag) => (
                      <span key={tag} className="rounded-md border border-app-line bg-app-panel2 px-2 py-1 text-xs text-slate-300">
                        {tag}
                      </span>
                    ))}
                  </div>
                  <div className="grid grid-cols-2 gap-2 text-sm">
                    <div className="rounded-lg bg-[#0b1118] p-3">
                      <div className="text-xs text-slate-500">材质</div>
                      <div className="mt-1 font-semibold">{formatNumber(summary.materials)}</div>
                    </div>
                    <div className="rounded-lg bg-[#0b1118] p-3">
                      <div className="text-xs text-slate-500">贴图</div>
                      <div className="mt-1 font-semibold">{formatNumber(summary.textures)}</div>
                    </div>
                    <div className="rounded-lg bg-[#0b1118] p-3">
                      <div className="text-xs text-slate-500">Morph</div>
                      <div className="mt-1 font-semibold">{formatNumber(summary.morphTargets)}</div>
                    </div>
                    <div className="rounded-lg bg-[#0b1118] p-3">
                      <div className="text-xs text-slate-500">VRM 表情</div>
                      <div className="mt-1 font-semibold">{formatNumber(summary.vrmExpressions)}</div>
                    </div>
                  </div>
                  <div className="scrollbar-soft max-h-44 overflow-auto rounded-lg border border-app-line">
                    {(inspect.meshes ?? []).slice(0, 12).map((mesh) => (
                      <div key={`${mesh.name}-${mesh.triangles}`} className="border-b border-app-line px-3 py-2 last:border-0">
                        <div className="truncate text-sm font-medium text-slate-200">{mesh.name || "Unnamed mesh"}</div>
                        <div className="mt-1 text-xs text-slate-500">
                          {formatNumber(mesh.triangles)} triangles · {mesh.skinned ? "skinned" : "static"} · {mesh.material?.mtoon ? "MToon" : "PBR"}
                        </div>
                      </div>
                    ))}
                  </div>
                </div>
              ) : (
                <div className="rounded-lg border border-dashed border-app-line p-5 text-center text-sm text-slate-500">
                  导入模型后显示资源、骨骼、材质和动画概况。
                </div>
              )}
            </Panel>

            <Panel title="转换结果">
              {convertResult ? (
                <div className="space-y-3 text-sm">
                  <div className={`rounded-lg border px-3 py-2 ${convertResult.ok ? "border-emerald-400/30 bg-emerald-500/10 text-emerald-200" : "border-red-400/30 bg-red-500/10 text-red-200"}`}>
                    {convertResult.ok ? "转换成功" : "转换失败"}
                  </div>
                  <div className="rounded-lg bg-[#0b1118] p-3 text-slate-300">
                    <div className="mb-1 text-xs text-slate-500">状态</div>
                    {convertResult.status ?? convertResult.error ?? "无状态信息"}
                  </div>
                  {convertResult.outputPath ? (
                    <div className="rounded-lg bg-[#0b1118] p-3 text-slate-300">
                      <div className="mb-1 text-xs text-slate-500">输出</div>
                      <div className="truncate">{convertResult.outputPath}</div>
                    </div>
                  ) : null}
                </div>
              ) : (
                <div className="flex items-center gap-2 rounded-lg border border-dashed border-app-line p-5 text-sm text-slate-500">
                  <ArrowRight className="h-4 w-4" />
                  执行转换后显示输出模型和报告。
                </div>
              )}
            </Panel>
          </aside>
        </div>
      </main>
    </div>
  );
}
