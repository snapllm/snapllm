export interface LoadedModelRef {
  id: string;
}

/** Resolve a request route from the latest server-authoritative loaded set. */
export function resolveLoadedModelId(
  selectedModelId: string,
  loadedModels: LoadedModelRef[],
): string {
  if (loadedModels.some((model) => model.id === selectedModelId)) {
    return selectedModelId;
  }
  return loadedModels.length === 1 ? loadedModels[0].id : '';
}
