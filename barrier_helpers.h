#pragma once

#include <d3d12.h>

D3D12_RESOURCE_BARRIER CD3DX12_Transition(
	_In_ ID3D12Resource* const pResource,
	const D3D12_RESOURCE_STATES stateBefore,
	const D3D12_RESOURCE_STATES stateAfter,
	const UINT subresource,
	const D3D12_RESOURCE_BARRIER_FLAGS flags);