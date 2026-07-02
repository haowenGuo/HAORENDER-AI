export type CoreSummary = {
  meshes?: number;
  vertices?: number;
  triangles?: number;
  nodes?: number;
  animations?: number;
  materials?: number;
  textures?: number;
  skinnedMeshes?: number;
  morphTargets?: number;
  vrmExpressions?: number;
  skeletonBones?: number;
  skinnedBones?: number;
  recognizedHumanoidBones?: number;
};

export type CoreInspectResult = {
  ok: boolean;
  command?: string;
  source?: string;
  fileUrl?: string;
  format?: string;
  error?: string;
  summary?: CoreSummary;
  materialTags?: string[];
  meshes?: Array<{
    name: string;
    vertices: number;
    triangles: number;
    skinned: boolean;
    skinBones: number;
    morphTargets: number;
    material?: {
      name?: string;
      mtoon?: boolean;
      unlit?: boolean;
      metallic?: number;
      roughness?: number;
      hasBaseColorTexture?: boolean;
      hasNormalTexture?: boolean;
    };
  }>;
  animations?: Array<{
    name: string;
    durationSeconds: number;
    channels: number;
    expressionChannels: number;
  }>;
  vrmExpressions?: Array<{
    name: string;
    morphTargetBinds: number;
    isBinary: boolean;
  }>;
  skeleton?: {
    boneCount?: number;
    recognizedBoneCount?: number;
    skinnedBoneCount?: number;
    bones?: Array<{
      name: string;
      canonicalName?: string;
      parentIndex?: number;
      referencedBySkin?: boolean;
      hasAnimationChannel?: boolean;
    }>;
  };
  stderr?: string;
  corePath?: string;
};

export type CorePreviewResult = {
  ok: boolean;
  previewPath?: string;
  previewUrl?: string;
  status?: string;
  error?: string;
};

export type CoreConvertResult = {
  ok: boolean;
  source?: string;
  output?: string;
  outputPath?: string;
  outputFileUrl?: string;
  targetFormat?: string;
  status?: string;
  error?: string;
  inspect?: CoreInspectResult;
  stderr?: string;
  corePath?: string;
};

export type RenderControlOption = {
  id: string;
  label: string;
};

export type RenderControl = {
  type: "range" | "toggle" | "select" | "color";
  id: string;
  label: string;
  section: string;
  value?: string | number | boolean;
  min?: number;
  max?: number;
  step?: number;
  options?: RenderControlOption[];
};

export type RenderPipelineCapability = {
  id: string;
  label: string;
  backend: string;
  device: string;
  status: string;
  note: string;
};

export type RenderCapabilitiesResult = {
  ok: boolean;
  command?: string;
  error?: string;
  uiContract?: string;
  pipelines?: RenderPipelineCapability[];
  sharedControls?: RenderControl[];
  rasterControls?: RenderControl[];
  rayTraceControls?: RenderControl[];
  captureActions?: RenderControlOption[];
};

export type RigMapResult = {
  ok: boolean;
  command?: string;
  targetPath?: string;
  sourcePath?: string;
  summary?: string;
  mappedCount?: number;
  unmappedSourceCount?: number;
  unmappedTargetCount?: number;
  targetError?: string;
  sourceError?: string;
  error?: string;
  mapping?: {
    mappings?: Array<{
      sourceBone: string;
      targetBone: string;
      canonicalName: string;
      confidence: number;
      reason: string;
    }>;
    source?: {
      boneCount?: number;
      recognizedBoneCount?: number;
    };
    target?: {
      boneCount?: number;
      recognizedBoneCount?: number;
    };
  };
};

export type HaoRenderBridge = {
  openModel(): Promise<string | null>;
  openRigAsset(role: "target" | "source"): Promise<string | null>;
  chooseOutput(sourcePath: string, targetFormat: string): Promise<string | null>;
  listFormats(): Promise<unknown>;
  renderCapabilities(): Promise<RenderCapabilitiesResult>;
  inspectModel(sourcePath: string): Promise<CoreInspectResult>;
  renderPreview(sourcePath: string): Promise<CorePreviewResult>;
  convertModel(payload: {
    sourcePath: string;
    targetFormat: string;
    outputPath?: string;
  }): Promise<CoreConvertResult>;
  rigMap(payload: {
    targetPath: string;
    sourcePath: string;
  }): Promise<RigMapResult>;
  toFileUrl(filePath: string): Promise<string>;
  showItemInFolder(filePath: string): Promise<void>;
};

declare global {
  interface Window {
    haoRender: HaoRenderBridge;
  }
}
