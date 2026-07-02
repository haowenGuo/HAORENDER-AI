import { useEffect, useMemo, useState } from "react";
import type { ComponentType, ReactNode } from "react";
import {
  ArrowRight,
  Box,
  CheckCircle2,
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
import type {
  CoreConvertResult,
  CoreInspectResult,
  CorePreviewResult,
  RenderCapabilitiesResult,
  RenderControl,
  RenderPipelineCapability,
  RigMapResult
} from "./types";

type Workspace = "convert" | "render" | "rig" | "inspect" | "compare" | "report";
type IconType = ComponentType<{ className?: string }>;

const targetFormats = ["glb", "gltf", "fbx", "obj", "dae", "stl", "ply", "vrm"];

const navItems: Array<{ id: Workspace; label: string; icon: IconType }> = [
  { id: "convert", label: "模型转换", icon: ArrowRight },
  { id: "render", label: "渲染工作台", icon: MonitorPlay },
  { id: "rig", label: "骨骼映射", icon: Share2 },
  { id: "inspect", label: "资源检查", icon: FileSearch },
  { id: "compare", label: "前后对比", icon: Layers3 },
  { id: "report", label: "转换报告", icon: CheckCircle2 }
];

function formatNumber(value?: number) {
  return new Intl.NumberFormat("zh-CN").format(value ?? 0);
}

function fileName(path?: string) {
  if (!path) return "未选择";
  return path.split(/[\\/]/).pop() ?? path;
}

function fileExtension(path?: string) {
  if (!path) return "";
  return path.split(".").pop()?.toUpperCase() ?? "";
}

function isThreePreviewable(path?: string) {
  const ext = fileExtension(path).toLowerCase();
  return ext === "glb" || ext === "gltf" || ext === "vrm";
}

function Button({
  children,
  onClick,
  disabled,
  tone = "default"
}: {
  children: ReactNode;
  onClick?: () => void;
  disabled?: boolean;
  tone?: "default" | "primary" | "quiet";
}) {
  const toneClass =
    tone === "primary"
      ? "button-primary"
      : tone === "quiet"
        ? "button-quiet"
        : "";

  return (
    <button
      className={`button no-drag inline-flex items-center justify-center gap-2 px-3 text-sm font-medium transition disabled:cursor-not-allowed disabled:opacity-45 ${toneClass}`}
      disabled={disabled}
      onClick={onClick}
    >
      {children}
    </button>
  );
}

function Panel({ title, children, action }: { title: string; children: ReactNode; action?: ReactNode }) {
  return (
    <section className="panel">
      <div className="panel-header flex items-center justify-between px-4">
        <h2 className="text-sm font-semibold text-slate-100">{title}</h2>
        {action}
      </div>
      <div className="panel-body">{children}</div>
    </section>
  );
}

function Metric({ label, value, icon: Icon }: { label: string; value?: number; icon: IconType }) {
  return (
    <div className="metric-card p-3">
      <div className="flex items-center gap-2 text-xs text-slate-400">
        <Icon className="h-3.5 w-3.5" />
        {label}
      </div>
      <div className="mt-2 text-xl font-semibold tracking-normal text-app-text">{formatNumber(value)}</div>
    </div>
  );
}

function FileCard({ label, path }: { label: string; path?: string }) {
  return (
    <div>
      <div className="mb-2 text-xs font-medium text-slate-400">{label}</div>
      <div className="file-card px-3 py-2 text-sm text-slate-200">
        <div className="truncate">{fileName(path)}</div>
        <div className="mt-1 truncate text-xs text-slate-500">{path || "未选择"}</div>
      </div>
    </div>
  );
}

function Empty({ children }: { children: ReactNode }) {
  return <div className="empty-state p-5 text-center text-sm">{children}</div>;
}

function planFromInspect(inspect: CoreInspectResult | null, targetFormat: string) {
  if (!inspect?.ok) return ["等待模型", "读取资源", "生成转换计划", "执行后台转换"];
  const summary = inspect.summary ?? {};
  const steps = [
    `${fileExtension(inspect.source)} -> ${targetFormat.toUpperCase()}`,
    `${formatNumber(summary.meshes)} meshes / ${formatNumber(summary.triangles)} triangles`,
    `${formatNumber(summary.skeletonBones)} bones / ${formatNumber(summary.animations)} animations`,
    `${formatNumber(summary.materials)} materials / ${formatNumber(summary.textures)} textures`
  ];
  if (targetFormat === "vrm") steps.push("VRM humanoid / expression metadata");
  if (["obj", "stl", "ply"].includes(targetFormat)) steps.push("geometry-only export risk");
  return steps;
}

export default function AppShell() {
  const [workspace, setWorkspace] = useState<Workspace>("render");
  const [sourcePath, setSourcePath] = useState("");
  const [targetFormat, setTargetFormat] = useState("glb");
  const [outputPath, setOutputPath] = useState("");
  const [inspect, setInspect] = useState<CoreInspectResult | null>(null);
  const [sourcePreview, setSourcePreview] = useState<CorePreviewResult | null>(null);
  const [convertResult, setConvertResult] = useState<CoreConvertResult | null>(null);
  const [convertedPreview, setConvertedPreview] = useState<CorePreviewResult | null>(null);
  const [rigTargetPath, setRigTargetPath] = useState("");
  const [rigSourcePath, setRigSourcePath] = useState("");
  const [rigResult, setRigResult] = useState<RigMapResult | null>(null);
  const [renderCapabilities, setRenderCapabilities] = useState<RenderCapabilitiesResult | null>(null);
  const [busy, setBusy] = useState("");
  const [logs, setLogs] = useState<string[]>(["HAORENDER-AI Desktop 已就绪。"]);

  const summary = inspect?.summary ?? {};
  const outputModelPath = convertResult?.outputPath || convertResult?.output || outputPath;
  const activeNav = navItems.find((item) => item.id === workspace) ?? navItems[0];
  const plan = useMemo(() => planFromInspect(inspect, targetFormat), [inspect, targetFormat]);

  useEffect(() => {
    let alive = true;
    window.haoRender
      .renderCapabilities()
      .then((result) => {
        if (alive) setRenderCapabilities(result);
      })
      .catch(() => {
        if (alive) setRenderCapabilities({ ok: false, error: "Failed to query render capabilities." });
      });
    return () => {
      alive = false;
    };
  }, []);

  function pushLog(message: string) {
    setLogs((items) => [`${new Date().toLocaleTimeString("zh-CN")}  ${message}`, ...items].slice(0, 80));
  }

  async function inspectAndPreview(path: string) {
    setBusy("inspect");
    pushLog(`分析模型：${path}`);
    try {
      const result = await window.haoRender.inspectModel(path);
      setInspect(result);
      pushLog(result.ok ? "模型概况读取完成。" : `模型读取失败：${result.error ?? result.stderr ?? "未知错误"}`);
      const preview = await window.haoRender.renderPreview(path);
      setSourcePreview(preview);
      if (preview.ok) pushLog("后台渲染预览已生成。");
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

  async function refreshPreview() {
    if (!sourcePath) return;
    setBusy("preview");
    pushLog(`刷新后台预览：${sourcePath}`);
    try {
      const preview = await window.haoRender.renderPreview(sourcePath);
      setSourcePreview(preview);
      pushLog(preview.ok ? "后台预览刷新完成。" : `后台预览失败：${preview.error ?? preview.status ?? "未知错误"}`);
    } finally {
      setBusy("");
    }
  }

  async function chooseOutput() {
    if (!sourcePath) return;
    const path = await window.haoRender.chooseOutput(sourcePath, targetFormat);
    if (path) setOutputPath(path);
  }

  async function convertModel() {
    if (!sourcePath) return;
    setBusy("convert");
    setWorkspace("compare");
    pushLog(`转换为 ${targetFormat.toUpperCase()}。`);
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

  async function openRigAsset(role: "target" | "source") {
    const path = await window.haoRender.openRigAsset(role);
    if (!path) return;
    if (role === "target") setRigTargetPath(path);
    else setRigSourcePath(path);
    setRigResult(null);
    pushLog(`${role === "target" ? "目标人物" : "源动画"}：${path}`);
  }

  async function generateRigMap() {
    if (!rigTargetPath || !rigSourcePath) return;
    setBusy("rig");
    pushLog("生成骨骼映射。");
    try {
      const result = await window.haoRender.rigMap({ targetPath: rigTargetPath, sourcePath: rigSourcePath });
      setRigResult(result);
      pushLog(result.ok ? `映射完成：${result.summary ?? `${result.mappedCount ?? 0} bones`}` : `映射失败：${result.error ?? result.targetError ?? result.sourceError ?? "未知错误"}`);
    } finally {
      setBusy("");
    }
  }

  const headerActions =
    workspace === "rig" ? (
      <>
        <Button onClick={() => openRigAsset("target")} disabled={Boolean(busy)}>
          <FolderOpen className="h-4 w-4" />
          目标人物
        </Button>
        <Button onClick={() => openRigAsset("source")} disabled={Boolean(busy)}>
          <FolderOpen className="h-4 w-4" />
          源动画
        </Button>
        <Button tone="primary" onClick={generateRigMap} disabled={!rigTargetPath || !rigSourcePath || Boolean(busy)}>
          {busy === "rig" ? <Loader2 className="h-4 w-4 animate-spin" /> : <Share2 className="h-4 w-4" />}
          生成映射
        </Button>
      </>
    ) : workspace === "render" ? (
      <>
        <Button onClick={openModel} disabled={Boolean(busy)}>
          <FolderOpen className="h-4 w-4" />
          导入模型
        </Button>
        <Button tone="primary" onClick={refreshPreview} disabled={!sourcePath || Boolean(busy)}>
          {busy === "preview" ? <Loader2 className="h-4 w-4 animate-spin" /> : <MonitorPlay className="h-4 w-4" />}
          后台渲染
        </Button>
      </>
    ) : (
      <>
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
      </>
    );

  return (
    <div className="app-shell flex h-screen">
      <aside className="app-sidebar flex w-[276px] shrink-0 flex-col">
        <div className="drag-region flex h-20 items-center px-5">
          <div>
            <div className="flex items-center gap-2 text-lg font-semibold">
              <Sparkles className="brand-mark h-5 w-5" />
              HAORENDER-AI
            </div>
            <div className="mt-1 text-xs text-stone-500">Render Core Workbench</div>
          </div>
        </div>
        <nav className="space-y-1 px-3">
          {navItems.map((item) => {
            const Icon = item.icon;
            const active = item.id === workspace;
            return (
              <button
                key={item.id}
                className={`nav-button no-drag flex h-10 w-full items-center gap-3 rounded-lg px-3 text-left text-sm transition ${active ? "nav-button-active" : ""}`}
                onClick={() => setWorkspace(item.id)}
              >
                <Icon className="h-4 w-4" />
                {item.label}
              </button>
            );
          })}
        </nav>
        <div className="mt-auto border-t border-[#262b24] p-4 text-xs leading-6 text-stone-500">
          <div>后台核心</div>
          <div className="truncate text-stone-300">{convertResult?.corePath ?? inspect?.corePath ?? "haorender-core.exe"}</div>
        </div>
      </aside>

      <main className="flex min-w-0 flex-1 flex-col">
        <header className="workspace-header drag-region flex h-20 items-center justify-between px-6">
          <div>
            <h1 className="text-xl font-semibold tracking-normal">{activeNav.label}</h1>
            <p className="mt-1 text-sm text-stone-500">{workspace === "rig" ? fileName(rigTargetPath || rigSourcePath) : fileName(sourcePath)}</p>
          </div>
          <div className="no-drag flex items-center gap-2">{headerActions}</div>
        </header>

        {workspace === "render" ? (
          <RenderWorkspace sourcePath={sourcePath} inspect={inspect} sourcePreview={sourcePreview} busy={busy} openModel={openModel} refreshPreview={refreshPreview} logs={logs} capabilities={renderCapabilities} />
        ) : workspace === "rig" ? (
          <RigWorkspace
            rigTargetPath={rigTargetPath}
            rigSourcePath={rigSourcePath}
            rigResult={rigResult}
            busy={busy}
            logs={logs}
            openRigAsset={openRigAsset}
            generateRigMap={generateRigMap}
          />
        ) : workspace === "inspect" ? (
          <InspectWorkspace inspect={inspect} logs={logs} />
        ) : workspace === "compare" ? (
          <CompareWorkspace sourcePath={sourcePath} targetFormat={targetFormat} inspect={inspect} sourcePreview={sourcePreview} convertResult={convertResult} convertedPreview={convertedPreview} outputModelPath={outputModelPath} />
        ) : workspace === "report" ? (
          <ReportWorkspace inspect={inspect} convertResult={convertResult} logs={logs} />
        ) : (
          <ConvertWorkspace
            sourcePath={sourcePath}
            targetFormat={targetFormat}
            outputPath={outputPath}
            inspect={inspect}
            sourcePreview={sourcePreview}
            convertResult={convertResult}
            convertedPreview={convertedPreview}
            outputModelPath={outputModelPath}
            plan={plan}
            logs={logs}
            chooseOutput={chooseOutput}
            setTargetFormat={setTargetFormat}
            resetConverted={() => {
              setOutputPath("");
              setConvertResult(null);
              setConvertedPreview(null);
            }}
          />
        )}
      </main>
    </div>
  );
}

function ConvertWorkspace({
  sourcePath,
  targetFormat,
  outputPath,
  inspect,
  sourcePreview,
  convertResult,
  convertedPreview,
  outputModelPath,
  plan,
  logs,
  chooseOutput,
  setTargetFormat,
  resetConverted
}: {
  sourcePath: string;
  targetFormat: string;
  outputPath: string;
  inspect: CoreInspectResult | null;
  sourcePreview: CorePreviewResult | null;
  convertResult: CoreConvertResult | null;
  convertedPreview: CorePreviewResult | null;
  outputModelPath?: string;
  plan: string[];
  logs: string[];
  chooseOutput: () => void;
  setTargetFormat: (format: string) => void;
  resetConverted: () => void;
}) {
  const sourceExt = fileExtension(sourcePath) || "SRC";
  const activeStep = convertResult ? plan.length - 1 : inspect?.ok ? 2 : sourcePath ? 1 : 0;
  const outputLabel = outputPath || outputModelPath || "自动生成输出路径";

  return (
    <div className="scrollbar-soft flex min-h-0 flex-1 flex-col gap-4 overflow-auto p-4">
      <DomainHero
        kind="convert"
        eyebrow="Asset Conversion"
        title="模型格式转换管线"
        subtitle="围绕网格、材质、骨骼、动画和 morph 的保真度来组织转换，而不是只做文件后缀替换。"
        meta={`${sourceExt} -> ${targetFormat.toUpperCase()}`}
      />

      <div className="grid min-h-0 grid-cols-[minmax(0,1fr)_380px] gap-4">
        <section className="flex min-w-0 flex-col gap-4">
          <div className="domain-flow-grid">
            <AssetTile label="输入" title={fileName(sourcePath)} detail={sourcePath || "等待导入 FBX / GLB / VRM / OBJ"} icon={Box} tone={sourcePath ? "good" : "muted"} />
            <AssetTile label="解析" title={inspect?.ok ? "资源已分析" : "等待检查"} detail={`${formatNumber(inspect?.summary?.meshes)} meshes / ${formatNumber(inspect?.summary?.materials)} materials`} icon={FileSearch} tone={inspect?.ok ? "good" : "muted"} />
            <AssetTile label="目标" title={targetFormat.toUpperCase()} detail="格式能力与材质策略" icon={ArrowRight} tone="info" />
            <AssetTile label="输出" title={fileName(outputModelPath || outputPath)} detail={outputLabel} icon={CheckCircle2} tone={convertResult?.ok ? "good" : "muted"} />
          </div>

          <div className="grid min-h-[380px] flex-1 grid-cols-2 gap-4">
            <ModelPreview label={`转换前 ${sourceExt}`} path={sourcePath} inspect={inspect} preview={sourcePreview} />
            <ModelPreview label={`转换后 ${targetFormat.toUpperCase()}`} path={outputModelPath} fileUrl={convertResult?.outputFileUrl} preview={convertedPreview} />
          </div>

          <Panel title="转换路线" action={<StatusPill label={convertResult?.ok ? "已完成" : inspect?.ok ? "已规划" : "等待输入"} tone={convertResult?.ok ? "good" : inspect?.ok ? "info" : "muted"} />}>
            <ProcessRail steps={plan} activeIndex={activeStep} />
          </Panel>

          <StatsGrid summary={inspect?.summary} />
          <LogPanel logs={logs} />
        </section>

        <aside className="flex min-h-0 flex-col gap-4">
          <Panel title="格式目标">
            <div className="space-y-4">
              <FileCard label="源文件" path={sourcePath} />
              <div>
                <div className="mb-2 text-xs font-medium text-slate-400">目标格式</div>
                <select
                  className="h-10 w-full rounded-lg border border-app-line bg-app-panel2 px-3 text-sm text-slate-100 outline-none focus:border-app-accent2"
                  value={targetFormat}
                  onChange={(event) => {
                    setTargetFormat(event.target.value);
                    resetConverted();
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
                  <div className="file-card min-w-0 flex-1 px-3 py-2 text-sm text-slate-300">
                    <div className="truncate">{outputPath || "HAORENDER-AI-Converted"}</div>
                  </div>
                  <Button onClick={chooseOutput} disabled={!sourcePath}>
                    选择
                  </Button>
                </div>
              </div>
            </div>
          </Panel>

          <Panel title="保真度关注">
            <div className="space-y-2">
              <ArtifactRow label="几何与层级" value={inspect?.ok ? `${formatNumber(inspect.summary?.nodes)} nodes` : "等待检查"} tone={inspect?.ok ? "good" : "muted"} />
              <ArtifactRow label="材质贴图" value={inspect?.ok ? `${formatNumber(inspect.summary?.materials)} / ${formatNumber(inspect.summary?.textures)}` : "等待检查"} tone={inspect?.summary?.materials ? "good" : "warn"} />
              <ArtifactRow label="骨骼动画" value={inspect?.ok ? `${formatNumber(inspect.summary?.skeletonBones)} bones / ${formatNumber(inspect.summary?.animations)} clips` : "等待检查"} tone={inspect?.summary?.skeletonBones ? "info" : "muted"} />
              <ArtifactRow label="Morph / 表情" value={inspect?.ok ? `${formatNumber(inspect.summary?.morphTargets)} morphs / ${formatNumber(inspect.summary?.vrmExpressions)} VRM` : "等待检查"} tone={inspect?.summary?.morphTargets ? "info" : "muted"} />
            </div>
          </Panel>

          <ResourcePanel inspect={inspect} />
          <ResultPanel result={convertResult} />
        </aside>
      </div>
    </div>
  );
}

function RenderWorkspace({
  sourcePath,
  inspect,
  sourcePreview,
  busy,
  openModel,
  refreshPreview,
  logs,
  capabilities
}: {
  sourcePath: string;
  inspect: CoreInspectResult | null;
  sourcePreview: CorePreviewResult | null;
  busy: string;
  openModel: () => void;
  refreshPreview: () => void;
  logs: string[];
  capabilities: RenderCapabilitiesResult | null;
}) {
  const [pipeline, setPipeline] = useState("raster");
  const [shadingModel, setShadingModel] = useState("pbr");
  const [controlValues, setControlValues] = useState<Record<string, string | number | boolean>>({});
  const pipelines = capabilities?.pipelines?.length ? capabilities.pipelines : fallbackPipelines();
  const activePipeline = pipelines.find((item) => item.id === pipeline) ?? pipelines[0];
  const rayActive = pipeline === "opengl-ray-trace" || pipeline === "dxr-hardware-rt";
  const rasterActive = pipeline === "raster";
  const cpuActive = pipeline === "cpu-software";
  const sharedControls = (capabilities?.sharedControls ?? []).filter((control) => control.id !== "renderPipeline" && control.id !== "shadingModel");
  const rasterControls = filterRasterControls(capabilities?.rasterControls ?? [], shadingModel);
  const rayControls = capabilities?.rayTraceControls ?? [];
  const visibleControls = rayActive ? rayControls : rasterActive ? rasterControls : [];

  function setControlValue(id: string, value: string | number | boolean) {
    setControlValues((items) => ({ ...items, [id]: value }));
  }

  return (
    <div className="grid min-h-0 flex-1 grid-cols-[minmax(0,1fr)_440px] gap-4 p-4">
      <section className="flex min-w-0 flex-col gap-4">
        <div className="min-h-[480px] flex-1">
          <ModelPreview label={`${activePipeline.label} ${fileExtension(sourcePath)}`} path={sourcePath} inspect={inspect} preview={sourcePreview} />
        </div>
        <div className="grid grid-cols-4 gap-3">
          {pipelines.map((item) => (
            <PipelineCard key={item.id} pipeline={item} active={item.id === pipeline} onClick={() => setPipeline(item.id)} />
          ))}
        </div>
        <StatsGrid summary={inspect?.summary} />
        <LogPanel logs={logs} />
      </section>

      <aside className="scrollbar-soft flex min-h-0 flex-col gap-4 overflow-auto pr-1">
        <Panel title="预览输入">
          <div className="space-y-4">
            <FileCard label="模型" path={sourcePath} />
            <div className="flex gap-2">
              <Button onClick={openModel} disabled={Boolean(busy)}>
                <FolderOpen className="h-4 w-4" />
                导入
              </Button>
              <Button tone="primary" onClick={refreshPreview} disabled={!sourcePath || Boolean(busy)}>
                {busy === "preview" ? <Loader2 className="h-4 w-4 animate-spin" /> : <MonitorPlay className="h-4 w-4" />}
                后台渲染
              </Button>
            </div>
          </div>
        </Panel>

        <Panel title="渲染路径">
          <div className="space-y-3">
            <SelectLine label="Render path" value={pipeline} onChange={setPipeline} options={pipelines.map((item) => ({ id: item.id, label: item.label }))} />
            <div className="sub-card p-3 text-sm leading-6 text-slate-300">
              <div className="font-semibold text-slate-100">{activePipeline.backend}</div>
              <div className="text-xs text-slate-500">{activePipeline.device} · {activePipeline.status}</div>
              <div className="mt-2 text-xs text-slate-400">{activePipeline.note}</div>
            </div>
          </div>
        </Panel>

        {rasterActive ? (
          <Panel title="Raster / LookDev">
            <div className="space-y-4">
              <SelectLine
                label="Shading model"
                value={shadingModel}
                onChange={setShadingModel}
                options={[
                  { id: "pbr", label: "PBR" },
                  { id: "phong", label: "Phong / Toon" }
                ]}
              />
              <RenderControlGroups controls={sharedControls} values={controlValues} onChange={setControlValue} />
            </div>
          </Panel>
        ) : null}

        {rayActive ? (
          <Panel title="Ray Trace / DXR">
            <RenderControlGroups controls={[...sharedControls, ...visibleControls]} values={controlValues} onChange={setControlValue} />
          </Panel>
        ) : null}

        {rasterActive ? (
          <Panel title={shadingModel === "pbr" ? "PBR / GPU Raster Controls" : "Phong / Toon / Outline Controls"}>
            <RenderControlGroups controls={visibleControls} values={controlValues} onChange={setControlValue} />
          </Panel>
        ) : null}

        {cpuActive ? (
          <Panel title="CPU Software Renderer">
            <div className="space-y-3 text-sm leading-6 text-slate-300">
              <div className="rounded-lg border border-amber-400/30 bg-amber-500/10 p-3 text-amber-100">
                CPU 软渲染核心不能丢，但它现在还没有被接成 Electron 后台服务。下一步应该把 legacy HaoRender CPU 路径做成命令行/服务接口，而不是打开 Qt。
              </div>
              <div className="sub-card p-3 text-slate-400">
                目标接口：加载模型、设置相机/材质/灯光、CPU 渲染输出帧、返回耗时和错误报告。
              </div>
            </div>
          </Panel>
        ) : null}

        <Panel title="迁移状态">
          <div className="space-y-2 text-sm leading-6 text-slate-300">
            <div>Electron 已接管完整渲染工作台的 UI 结构。</div>
            <div>当前后台截图仍走 `haorender-core --render-preview`；真正的实时 GPU/DXR viewport 需要下一步做 C++ render service 或 native texture/frame bridge。</div>
            <div className="text-slate-500">{capabilities?.uiContract ?? "正在等待 C++ 渲染能力表。"}</div>
          </div>
        </Panel>
      </aside>
    </div>
  );
}

function fallbackPipelines(): RenderPipelineCapability[] {
  return [
    {
      id: "raster",
      label: "Raster / OpenGL GPU",
      backend: "OpenGLRasterizer",
      device: "OpenGL GPU",
      status: "active",
      note: "PBR, Phong, Toon, outline and raster shadow controls."
    },
    {
      id: "opengl-ray-trace",
      label: "OpenGL Ray Trace",
      backend: "OpenGLRayTracer",
      device: "OpenGL 4.3 GPU",
      status: "active",
      note: "Hybrid RT, path tracing, NEE, photon cache and AOV views."
    },
    {
      id: "dxr-hardware-rt",
      label: "DXR Hardware RT",
      backend: "DxrRayTracer",
      device: "Direct3D 12 DXR",
      status: "active-when-supported",
      note: "Hardware ray tracing backend."
    },
    {
      id: "cpu-software",
      label: "CPU Software Renderer",
      backend: "Legacy HaoRender CPU path",
      device: "CPU",
      status: "bridge-required",
      note: "Preserved capability that must be wired as backend service."
    }
  ];
}

function filterRasterControls(controls: RenderControl[], shadingModel: string) {
  return controls.filter((control) => {
    if (control.id.startsWith("pbr.")) return shadingModel === "pbr";
    if (
      control.id.startsWith("phong.") ||
      control.id.startsWith("toon.") ||
      control.id.startsWith("outline.")
    ) {
      return shadingModel === "phong";
    }
    return control.id.startsWith("raster.");
  });
}

function PipelineCard({
  pipeline,
  active,
  onClick
}: {
  pipeline: RenderPipelineCapability;
  active: boolean;
  onClick: () => void;
}) {
  return (
    <button
      className={`pipeline-card no-drag p-3 ${active ? "pipeline-card-active" : ""}`}
      onClick={onClick}
    >
      <div className="text-sm font-semibold leading-5">{pipelineShortLabel(pipeline.label)}</div>
      <div className="mt-1 truncate text-xs text-slate-500">{pipeline.backend}</div>
      <div className="mt-3 text-xs text-slate-400">{pipeline.status}</div>
    </button>
  );
}

function pipelineShortLabel(label: string) {
  return label
    .replace("Raster / OpenGL GPU", "Raster GPU")
    .replace("OpenGL Ray Trace", "OpenGL RT")
    .replace("DXR Hardware RT", "DXR RT")
    .replace("CPU Software Renderer", "CPU Render");
}

function SelectLine({
  label,
  value,
  options,
  onChange
}: {
  label: string;
  value: string;
  options: Array<{ id: string; label: string }>;
  onChange: (value: string) => void;
}) {
  return (
    <label className="block">
      <div className="mb-2 text-xs font-medium text-slate-400">{label}</div>
      <select
        className="h-10 w-full rounded-lg border border-app-line bg-app-panel2 px-3 text-sm text-slate-100 outline-none focus:border-app-accent2"
        value={value}
        onChange={(event) => onChange(event.target.value)}
      >
        {options.map((item) => (
          <option key={item.id} value={item.id}>
            {item.label}
          </option>
        ))}
      </select>
    </label>
  );
}

function RenderControlGroups({
  controls,
  values,
  onChange
}: {
  controls: RenderControl[];
  values: Record<string, string | number | boolean>;
  onChange: (id: string, value: string | number | boolean) => void;
}) {
  const groups = groupControls(controls);
  if (!controls.length) return <Empty>等待 C++ 渲染参数 schema</Empty>;
  return (
    <div className="space-y-4">
      {groups.map((group) => (
        <div key={group.section} className="sub-card p-3">
          <div className="mb-3 text-sm font-semibold text-slate-100">{group.section}</div>
          <div className="space-y-3">
            {group.controls.map((control) => (
              <RenderControlField key={control.id} control={control} value={values[control.id] ?? control.value} onChange={(value) => onChange(control.id, value)} />
            ))}
          </div>
        </div>
      ))}
    </div>
  );
}

function groupControls(controls: RenderControl[]) {
  const sections: Array<{ section: string; controls: RenderControl[] }> = [];
  for (const control of controls) {
    let section = sections.find((item) => item.section === control.section);
    if (!section) {
      section = { section: control.section, controls: [] };
      sections.push(section);
    }
    section.controls.push(control);
  }
  return sections;
}

function RenderControlField({
  control,
  value,
  onChange
}: {
  control: RenderControl;
  value: string | number | boolean | undefined;
  onChange: (value: string | number | boolean) => void;
}) {
  if (control.type === "toggle") {
    return (
      <label className="flex items-center justify-between gap-3 text-sm text-slate-300">
        <span>{control.label}</span>
        <input className="h-4 w-4" type="checkbox" checked={Boolean(value)} onChange={(event) => onChange(event.target.checked)} />
      </label>
    );
  }
  if (control.type === "select") {
    return (
      <SelectLine
        label={control.label}
        value={String(value ?? "")}
        onChange={onChange}
        options={control.options ?? []}
      />
    );
  }
  if (control.type === "color") {
    return (
      <label className="flex items-center justify-between gap-3 text-sm text-slate-300">
        <span>{control.label}</span>
        <input
          className="h-8 w-12 rounded border border-app-line bg-transparent"
          type="color"
          value={String(value ?? "#ffffff")}
          onChange={(event) => onChange(event.target.value)}
        />
      </label>
    );
  }
  const numeric = typeof value === "number" ? value : Number(value ?? control.min ?? 0);
  return (
    <label className="block text-sm text-slate-300">
      <div className="mb-2 flex items-center justify-between gap-3">
        <span>{control.label}</span>
        <span className="font-mono text-xs text-slate-500">{Number.isFinite(numeric) ? numeric.toFixed(control.step && control.step < 0.01 ? 4 : 2) : value}</span>
      </div>
      <input
        className="w-full"
        type="range"
        min={control.min}
        max={control.max}
        step={control.step}
        value={Number.isFinite(numeric) ? numeric : 0}
        onChange={(event) => onChange(Number(event.target.value))}
      />
    </label>
  );
}

function RigWorkspace({
  rigTargetPath,
  rigSourcePath,
  rigResult,
  busy,
  logs,
  openRigAsset,
  generateRigMap
}: {
  rigTargetPath: string;
  rigSourcePath: string;
  rigResult: RigMapResult | null;
  busy: string;
  logs: string[];
  openRigAsset: (role: "target" | "source") => void;
  generateRigMap: () => void;
}) {
  const mappings = rigResult?.mapping?.mappings ?? [];
  const sourceBones = rigResult?.mapping?.source?.boneCount ?? 0;
  const targetBones = rigResult?.mapping?.target?.boneCount ?? 0;
  const mapped = rigResult?.mappedCount ?? mappings.length;
  const chainCards = [
    { name: "躯干链", hint: "hips / spine / chest / neck", count: countMappedBones(mappings, ["hips", "spine", "chest", "neck"]) },
    { name: "肩臂链", hint: "shoulder / upper arm / lower arm", count: countMappedBones(mappings, ["shoulder", "upperarm", "lowerarm", "arm"]) },
    { name: "手腕手指", hint: "hand / wrist / finger", count: countMappedBones(mappings, ["hand", "wrist", "thumb", "index", "middle", "ring", "little"]) },
    { name: "腿脚链", hint: "upper leg / lower leg / foot / toe", count: countMappedBones(mappings, ["upperleg", "lowerleg", "foot", "toe", "leg"]) },
    { name: "头眼表情", hint: "head / jaw / eye", count: countMappedBones(mappings, ["head", "jaw", "eye"]) }
  ];

  return (
    <div className="scrollbar-soft flex min-h-0 flex-1 flex-col gap-4 overflow-auto p-4">
      <DomainHero
        kind="rig"
        eyebrow="Rig Retargeting"
        title="骨架映射与动作重定向"
        subtitle="先确认目标人物和源动画的骨架语义，再检查躯干、肩臂、手腕、腿脚和眼部链路是否稳定。"
        meta={rigResult ? `${mapped} mapped / ${sourceBones || targetBones || "?"} bones` : "Humanoid Mapping"}
      />

      <div className="grid min-h-0 grid-cols-[400px_minmax(0,1fr)] gap-4">
        <aside className="flex min-h-0 flex-col gap-4">
          <Panel title="重定向输入">
            <div className="space-y-4">
              <AssetTile label="目标人物" title={fileName(rigTargetPath)} detail={rigTargetPath || "选择需要被驱动的 VRM / FBX / GLB 人物"} icon={Box} tone={rigTargetPath ? "good" : "muted"} />
              <Button onClick={() => openRigAsset("target")} disabled={Boolean(busy)}>
                <FolderOpen className="h-4 w-4" />
                选择目标人物
              </Button>
              <AssetTile label="源动画" title={fileName(rigSourcePath)} detail={rigSourcePath || "选择提供动作的 FBX / 动画资产"} icon={Play} tone={rigSourcePath ? "good" : "muted"} />
              <Button onClick={() => openRigAsset("source")} disabled={Boolean(busy)}>
                <FolderOpen className="h-4 w-4" />
                选择源动画
              </Button>
              <Button tone="primary" onClick={generateRigMap} disabled={!rigTargetPath || !rigSourcePath || Boolean(busy)}>
                {busy === "rig" ? <Loader2 className="h-4 w-4 animate-spin" /> : <Share2 className="h-4 w-4" />}
                生成映射
              </Button>
            </div>
          </Panel>

          <Panel title="映射质量">
            {rigResult ? (
              <div className="grid grid-cols-3 gap-2">
                <Metric label="已映射" value={rigResult.mappedCount} icon={CheckCircle2} />
                <Metric label="源缺失" value={rigResult.unmappedSourceCount} icon={FileSearch} />
                <Metric label="目标缺失" value={rigResult.unmappedTargetCount} icon={Layers3} />
              </div>
            ) : (
              <Empty>等待映射结果</Empty>
            )}
          </Panel>

          <Panel title="骨架链路">
            <div className="chain-grid">
              {chainCards.map((item) => (
                <ChainCard key={item.name} name={item.name} hint={item.hint} count={item.count} />
              ))}
            </div>
          </Panel>
        </aside>

        <section className="flex min-w-0 flex-col gap-4">
          <div className="rig-stage">
            <div>
              <div className="text-xs font-semibold uppercase tracking-[0.18em] text-stone-500">Retarget Graph</div>
              <h2 className="mt-2 text-lg font-semibold text-app-text">目标骨架与源动作对齐</h2>
              <p className="mt-2 max-w-[560px] text-sm leading-6 text-stone-400">
                这里会把 AI 名称映射、标准 humanoid 名称、置信度和缺失链路放在同一张图里，方便后续继续做姿态校正、IK 和质量评分。
              </p>
            </div>
            <RigDiagram mapped={mapped} sourceBones={sourceBones} targetBones={targetBones} />
          </div>

          <Panel title="骨骼映射表" action={<span className="text-xs text-slate-500">{mappings.length ? `${mappings.length} 条` : ""}</span>}>
            {mappings.length ? (
              <div className="scrollbar-soft max-h-[520px] overflow-auto rounded-lg border border-app-line">
                <table className="w-full border-collapse text-left text-sm">
                  <thead className="sticky top-0 bg-[#10110f] text-xs text-slate-400">
                    <tr>
                      <th className="px-3 py-2 font-medium">源骨骼</th>
                      <th className="px-3 py-2 font-medium">目标骨骼</th>
                      <th className="px-3 py-2 font-medium">标准名</th>
                      <th className="px-3 py-2 font-medium">置信度</th>
                      <th className="px-3 py-2 font-medium">依据</th>
                    </tr>
                  </thead>
                  <tbody>
                    {mappings.map((item, index) => (
                      <tr key={`${item.sourceBone}-${item.targetBone}-${index}`} className="border-t border-app-line text-slate-300">
                        <td className="px-3 py-2">{item.sourceBone}</td>
                        <td className="px-3 py-2">{item.targetBone}</td>
                        <td className="px-3 py-2 text-emerald-200">{item.canonicalName}</td>
                        <td className="px-3 py-2">{Math.round((item.confidence ?? 0) * 100)}%</td>
                        <td className="px-3 py-2 text-slate-500">{item.reason}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            ) : (
              <Empty>{rigResult?.ok === false ? rigResult.error ?? rigResult.targetError ?? rigResult.sourceError ?? "映射失败" : "等待目标人物和源动画"}</Empty>
            )}
          </Panel>

          <LogPanel logs={logs} />
        </section>
      </div>
    </div>
  );
}

function InspectWorkspace({ inspect, logs }: { inspect: CoreInspectResult | null; logs: string[] }) {
  return (
    <div className="grid min-h-0 flex-1 grid-cols-[minmax(0,1fr)_360px] gap-4 p-4">
      <ResourcePanel inspect={inspect} tall />
      <aside className="flex min-h-0 flex-col gap-4">
        <StatsGrid summary={inspect?.summary} compact />
        <LogPanel logs={logs} />
      </aside>
    </div>
  );
}

function CompareWorkspace({
  sourcePath,
  targetFormat,
  inspect,
  sourcePreview,
  convertResult,
  convertedPreview,
  outputModelPath
}: {
  sourcePath: string;
  targetFormat: string;
  inspect: CoreInspectResult | null;
  sourcePreview: CorePreviewResult | null;
  convertResult: CoreConvertResult | null;
  convertedPreview: CorePreviewResult | null;
  outputModelPath?: string;
}) {
  return (
    <div className="flex min-h-0 flex-1 flex-col gap-4 p-4">
      <div className="grid min-h-[520px] flex-1 grid-cols-2 gap-4">
        <ModelPreview label={`转换前 ${fileExtension(sourcePath)}`} path={sourcePath} inspect={inspect} preview={sourcePreview} />
        <ModelPreview label={`转换后 ${targetFormat.toUpperCase()}`} path={outputModelPath} fileUrl={convertResult?.outputFileUrl} preview={convertedPreview} />
      </div>
      <StatsGrid summary={inspect?.summary} />
    </div>
  );
}

function ReportWorkspace({ inspect, convertResult, logs }: { inspect: CoreInspectResult | null; convertResult: CoreConvertResult | null; logs: string[] }) {
  const summary = inspect?.summary ?? {};
  const output = convertResult?.outputPath ?? convertResult?.output ?? "";
  const statusTone = convertResult?.ok ? "good" : convertResult ? "warn" : "muted";
  const statusLabel = convertResult?.ok ? "可交付" : convertResult ? "需要处理" : "等待转换";
  const outputFormat = convertResult?.targetFormat?.toUpperCase() ?? (fileExtension(output) || "N/A");

  return (
    <div className="scrollbar-soft flex min-h-0 flex-1 flex-col gap-4 overflow-auto p-4">
      <DomainHero
        kind="report"
        eyebrow="Delivery Review"
        title="转换交付报告"
        subtitle="把输出文件、保真度风险、资源摘要和执行记录集中在一页，方便你判断这次转换能不能交给下游继续用。"
        meta={statusLabel}
      />

      <div className="grid grid-cols-4 gap-3">
        <AssetTile label="源模型" title={fileName(inspect?.source ?? convertResult?.source)} detail={inspect?.source ?? convertResult?.source ?? "等待导入模型"} icon={Box} tone={inspect?.ok ? "good" : "muted"} />
        <AssetTile label="输出模型" title={fileName(output)} detail={output || "等待生成输出"} icon={CheckCircle2} tone={convertResult?.ok ? "good" : "muted"} />
        <AssetTile label="格式状态" title={outputFormat} detail={convertResult?.status ?? "等待后台执行"} icon={ArrowRight} tone={statusTone} />
        <AssetTile label="资源规模" title={`${formatNumber(summary.triangles)} tris`} detail={`${formatNumber(summary.meshes)} meshes / ${formatNumber(summary.materials)} materials`} icon={Layers3} tone={inspect?.ok ? "info" : "muted"} />
      </div>

      <div className="grid min-h-0 grid-cols-[minmax(0,1fr)_400px] gap-4">
        <section className="flex min-w-0 flex-col gap-4">
          <Panel title="交付件清单" action={<StatusPill label={statusLabel} tone={statusTone} />}>
            <div className="artifact-list">
              <ArtifactRow label="模型文件" value={output || "未生成"} tone={convertResult?.ok ? "good" : "muted"} />
              <ArtifactRow label="转换报告" value={convertResult ? "已记录后台状态和输出路径" : "等待执行转换"} tone={convertResult ? "info" : "muted"} />
              <ArtifactRow label="材质概况" value={inspect?.ok ? `${formatNumber(summary.materials)} materials / ${formatNumber(summary.textures)} textures` : "等待资源检查"} tone={inspect?.summary?.materials ? "good" : "warn"} />
              <ArtifactRow label="动画与骨骼" value={inspect?.ok ? `${formatNumber(summary.animations)} clips / ${formatNumber(summary.skeletonBones)} bones` : "等待资源检查"} tone={summary.animations || summary.skeletonBones ? "info" : "muted"} />
              <ArtifactRow label="VRM / Morph" value={inspect?.ok ? `${formatNumber(summary.vrmExpressions)} expressions / ${formatNumber(summary.morphTargets)} morphs` : "等待资源检查"} tone={summary.vrmExpressions || summary.morphTargets ? "info" : "muted"} />
            </div>
          </Panel>

          <Panel title="下游风险">
            <div className="report-risk-grid">
              <div>
                <div className="text-xs font-semibold text-stone-500">Geometry</div>
                <p className="mt-2 text-sm leading-6 text-stone-300">{summary.meshes ? "网格已被识别，可以进行前后对比。" : "还没有可判断的网格信息。"}</p>
              </div>
              <div>
                <div className="text-xs font-semibold text-stone-500">Material</div>
                <p className="mt-2 text-sm leading-6 text-stone-300">{summary.materials ? "材质存在，下一步需要重点看贴图路径和 PBR/MToon 兼容。" : "未发现材质信息，输出可能只适合几何检查。"}</p>
              </div>
              <div>
                <div className="text-xs font-semibold text-stone-500">Animation</div>
                <p className="mt-2 text-sm leading-6 text-stone-300">{summary.animations ? "动画通道存在，建议转换后进入对比页检查姿态和 root 位移。" : "未发现动画，报告按静态模型交付判断。"}</p>
              </div>
            </div>
          </Panel>

          <LogPanel logs={logs} />
        </section>

        <aside className="flex min-h-0 flex-col gap-4">
          <ResultPanel result={convertResult} />
          <ResourcePanel inspect={inspect} />
        </aside>
      </div>
    </div>
  );
}

type DomainKind = "convert" | "rig" | "report";
type Tone = "good" | "warn" | "muted" | "info";

function DomainHero({
  kind,
  eyebrow,
  title,
  subtitle,
  meta
}: {
  kind: DomainKind;
  eyebrow: string;
  title: string;
  subtitle: string;
  meta: string;
}) {
  return (
    <section className={`domain-hero domain-hero-${kind}`}>
      <div className="min-w-0">
        <div className="text-xs font-semibold uppercase tracking-[0.18em] text-stone-500">{eyebrow}</div>
        <h2 className="mt-3 text-2xl font-semibold tracking-normal text-app-text">{title}</h2>
        <p className="mt-3 max-w-[720px] text-sm leading-6 text-stone-400">{subtitle}</p>
        <div className="mt-5">
          <StatusPill label={meta} tone={kind === "report" ? "info" : "good"} />
        </div>
      </div>
      <DomainVisual kind={kind} />
    </section>
  );
}

function DomainVisual({ kind }: { kind: DomainKind }) {
  if (kind === "convert") {
    return (
      <div className="domain-visual domain-visual-convert" aria-hidden="true">
        <div className="format-block">FBX</div>
        <div className="format-arrow" />
        <div className="format-core">CORE</div>
        <div className="format-arrow" />
        <div className="format-block">GLB</div>
      </div>
    );
  }
  if (kind === "rig") {
    return (
      <div className="domain-visual domain-visual-rig" aria-hidden="true">
        <div className="rig-stick rig-stick-left" />
        <div className="rig-stick rig-stick-right" />
        <div className="rig-link-line rig-link-1" />
        <div className="rig-link-line rig-link-2" />
        <div className="rig-link-line rig-link-3" />
      </div>
    );
  }
  return (
    <div className="domain-visual domain-visual-report" aria-hidden="true">
      <div className="report-sheet">
        <span />
        <span />
        <span />
      </div>
      <div className="report-badge" />
    </div>
  );
}

function StatusPill({ label, tone = "muted" }: { label: string; tone?: Tone }) {
  return <span className={`status-pill status-pill-${tone}`}>{label}</span>;
}

function AssetTile({
  label,
  title,
  detail,
  icon: Icon,
  tone = "muted"
}: {
  label: string;
  title: string;
  detail: string;
  icon: IconType;
  tone?: Tone;
}) {
  return (
    <div className={`asset-tile asset-tile-${tone}`}>
      <div className="flex items-center justify-between gap-3">
        <span className="text-xs font-semibold uppercase tracking-[0.14em] text-stone-500">{label}</span>
        <Icon className="h-4 w-4" />
      </div>
      <div className="mt-3 truncate text-sm font-semibold text-app-text">{title || "未选择"}</div>
      <div className="mt-1 truncate text-xs text-stone-500">{detail}</div>
    </div>
  );
}

function ArtifactRow({ label, value, tone = "muted" }: { label: string; value: string; tone?: Tone }) {
  return (
    <div className="artifact-row">
      <div className="min-w-0">
        <div className="text-sm font-medium text-app-text">{label}</div>
        <div className="mt-1 truncate text-xs text-stone-500">{value}</div>
      </div>
      <StatusPill label={toneLabel(tone)} tone={tone} />
    </div>
  );
}

function ProcessRail({ steps, activeIndex }: { steps: string[]; activeIndex: number }) {
  return (
    <div className="process-rail">
      {steps.map((step, index) => (
        <div key={`${index}-${step}`} className={`process-step ${index <= activeIndex ? "process-step-active" : ""}`}>
          <div className="process-dot">{index + 1}</div>
          <div className="min-w-0">
            <div className="truncate text-sm font-medium text-app-text">{step}</div>
            <div className="mt-1 text-xs text-stone-500">{index <= activeIndex ? "已进入流程" : "等待"}</div>
          </div>
        </div>
      ))}
    </div>
  );
}

function ChainCard({ name, hint, count }: { name: string; hint: string; count: number }) {
  const tone: Tone = count > 1 ? "good" : count === 1 ? "info" : "muted";
  return (
    <div className="chain-card">
      <div className="flex items-center justify-between gap-2">
        <div className="text-sm font-semibold text-app-text">{name}</div>
        <StatusPill label={`${count}`} tone={tone} />
      </div>
      <div className="mt-2 text-xs leading-5 text-stone-500">{hint}</div>
    </div>
  );
}

function RigDiagram({ mapped, sourceBones, targetBones }: { mapped: number; sourceBones: number; targetBones: number }) {
  return (
    <div className="rig-diagram" aria-hidden="true">
      <div className="rig-column">
        <div className="rig-node rig-node-head" />
        <div className="rig-node rig-node-body" />
        <div className="rig-limb rig-arm-left" />
        <div className="rig-limb rig-arm-right" />
        <div className="rig-limb rig-leg-left" />
        <div className="rig-limb rig-leg-right" />
        <div className="rig-count">{sourceBones || "SRC"}</div>
      </div>
      <div className="rig-transfer">
        <span />
        <span />
        <span />
        <strong>{mapped || 0}</strong>
      </div>
      <div className="rig-column rig-column-target">
        <div className="rig-node rig-node-head" />
        <div className="rig-node rig-node-body" />
        <div className="rig-limb rig-arm-left" />
        <div className="rig-limb rig-arm-right" />
        <div className="rig-limb rig-leg-left" />
        <div className="rig-limb rig-leg-right" />
        <div className="rig-count">{targetBones || "DST"}</div>
      </div>
    </div>
  );
}

function toneLabel(tone: Tone) {
  if (tone === "good") return "通过";
  if (tone === "warn") return "关注";
  if (tone === "info") return "记录";
  return "等待";
}

function countMappedBones(mappings: NonNullable<RigMapResult["mapping"]>["mappings"], keys: string[]) {
  const items = mappings ?? [];
  return items.filter((item) => {
    const text = `${item.canonicalName} ${item.sourceBone} ${item.targetBone}`.replace(/\s+/g, "").toLowerCase();
    return keys.some((key) => text.includes(key));
  }).length;
}

function ModelPreview({
  label,
  path,
  inspect,
  preview,
  fileUrl
}: {
  label: string;
  path?: string;
  inspect?: CoreInspectResult | null;
  preview?: CorePreviewResult | null;
  fileUrl?: string;
}) {
  return <ThreePreview label={label.trim()} fileUrl={isThreePreviewable(path) ? fileUrl ?? inspect?.fileUrl : undefined} fallbackImageUrl={preview?.previewUrl} />;
}

function StatsGrid({ summary, compact = false }: { summary: CoreInspectResult["summary"]; compact?: boolean }) {
  const children = (
    <>
      <Metric label="网格" value={summary?.meshes} icon={Box} />
      <Metric label="三角形" value={summary?.triangles} icon={Triangle} />
      <Metric label="骨骼" value={summary?.skeletonBones} icon={Layers3} />
      <Metric label="动画" value={summary?.animations} icon={Play} />
    </>
  );
  return <div className={`grid gap-3 ${compact ? "grid-cols-2" : "grid-cols-4"}`}>{children}</div>;
}

function PlanPanel({ plan }: { plan: string[] }) {
  return (
    <Panel title="AI 转换计划" action={<Wand2 className="h-4 w-4 text-app-accent2" />}>
      <ol className="space-y-3">
        {plan.map((step, index) => (
          <li key={`${index}-${step}`} className="flex gap-3 text-sm leading-6 text-slate-300">
            <span className="mt-0.5 flex h-6 w-6 shrink-0 items-center justify-center rounded-full bg-emerald-500/15 text-xs font-semibold text-emerald-200">{index + 1}</span>
            <span>{step}</span>
          </li>
        ))}
      </ol>
    </Panel>
  );
}

function LogPanel({ logs }: { logs: string[] }) {
  return (
    <Panel title="执行日志">
      <div className="scrollbar-soft sub-card h-40 overflow-auto p-3 font-mono text-xs leading-6 text-slate-300">
        {logs.map((log) => (
          <div key={log}>{log}</div>
        ))}
      </div>
    </Panel>
  );
}

function ResourcePanel({ inspect, tall = false }: { inspect: CoreInspectResult | null; tall?: boolean }) {
  const summary = inspect?.summary ?? {};
  return (
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
          <div className="grid grid-cols-4 gap-2 text-sm">
            <SmallMetric label="材质" value={summary.materials} />
            <SmallMetric label="贴图" value={summary.textures} />
            <SmallMetric label="Morph" value={summary.morphTargets} />
            <SmallMetric label="VRM 表情" value={summary.vrmExpressions} />
          </div>
          <div className={`scrollbar-soft overflow-auto rounded-lg border border-app-line ${tall ? "max-h-[620px]" : "max-h-44"}`}>
            {(inspect.meshes ?? []).map((mesh) => (
              <div key={`${mesh.name}-${mesh.triangles}-${mesh.vertices}`} className="border-b border-app-line px-3 py-2 last:border-0">
                <div className="truncate text-sm font-medium text-slate-200">{mesh.name || "Unnamed mesh"}</div>
                <div className="mt-1 text-xs text-slate-500">
                  {formatNumber(mesh.triangles)} triangles · {mesh.skinned ? "skinned" : "static"} · {mesh.material?.mtoon ? "MToon" : "PBR"}
                </div>
              </div>
            ))}
          </div>
        </div>
      ) : (
        <Empty>等待模型数据</Empty>
      )}
    </Panel>
  );
}

function SmallMetric({ label, value }: { label: string; value?: number }) {
  return (
    <div className="sub-card p-3">
      <div className="text-xs text-slate-500">{label}</div>
      <div className="mt-1 font-semibold">{formatNumber(value)}</div>
    </div>
  );
}

function ResultPanel({ result }: { result: CoreConvertResult | null }) {
  return (
    <Panel title="转换结果">
      {result ? (
        <div className="space-y-3 text-sm">
          <div className={`rounded-lg border px-3 py-2 ${result.ok ? "border-emerald-400/30 bg-emerald-500/10 text-emerald-200" : "border-red-400/30 bg-red-500/10 text-red-200"}`}>
            {result.ok ? "转换成功" : "转换失败"}
          </div>
          <div className="sub-card p-3 text-slate-300">
            <div className="mb-1 text-xs text-slate-500">状态</div>
            {result.status ?? result.error ?? "无状态信息"}
          </div>
          {result.outputPath ? (
            <div className="sub-card p-3 text-slate-300">
              <div className="mb-1 text-xs text-slate-500">输出</div>
              <div className="truncate">{result.outputPath}</div>
            </div>
          ) : null}
        </div>
      ) : (
        <Empty>等待转换结果</Empty>
      )}
    </Panel>
  );
}
