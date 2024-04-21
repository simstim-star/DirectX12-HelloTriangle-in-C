#include "barrier_helpers.h"

D3D12_RESOURCE_BARRIER CD3DX12_Transition(
	_In_ ID3D12Resource* const pResource,
	const D3D12_RESOURCE_STATES stateBefore,
	const D3D12_RESOURCE_STATES stateAfter,
	const UINT subresource,
	const D3D12_RESOURCE_BARRIER_FLAGS flags)
{
	D3D12_RESOURCE_BARRIER result = { 0 };
	result.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	result.Flags = flags;
	result.Transition.pResource = pResource;
	result.Transition.StateBefore = stateBefore;
	result.Transition.StateAfter = stateAfter;
	result.Transition.Subresource = subresource;
	return result;
}