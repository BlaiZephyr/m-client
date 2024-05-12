/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "mapimages.h"

#include <base/log.h>

#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/generated/client_data.h>
#include <game/layers.h>
#include <game/localization.h>
#include <game/mapitems.h>

const char *const gs_apModEntitiesNames[] = {
	"ddnet",
	"ddrace",
	"race",
	"blockworlds",
	"fng",
	"vanilla",
	"f-ddrace",
};

CMapImages::CMapImages() :
	CMapImages(100)
{
}

CMapImages::CMapImages(int TextureSize)
{
	m_Count = 0;
	m_TextureScale = TextureSize;
	mem_zero(m_aEntitiesIsLoaded, sizeof(m_aEntitiesIsLoaded));
	m_SpeedupArrowIsLoaded = false;

	mem_zero(m_aTextureUsedByTileOrQuadLayerFlag, sizeof(m_aTextureUsedByTileOrQuadLayerFlag));

	str_copy(m_aEntitiesPath, "editor/entities_clear");

	static_assert(std::size(gs_apModEntitiesNames) == MAP_IMAGE_MOD_TYPE_COUNT, "Mod name string count is not equal to mod type count");
}

void CMapImages::OnInit()
{
	InitOverlayTextures();

	if(str_comp(g_Config.m_ClAssetsEntities, "default") == 0)
		str_copy(m_aEntitiesPath, "editor/entities_clear");
	else
	{
		str_format(m_aEntitiesPath, sizeof(m_aEntitiesPath), "assets/entities/%s", g_Config.m_ClAssetsEntities);
	}
}

void CMapImages::OnMapLoadImpl(class CLayers *pLayers, IMap *pMap)
{
	// unload all textures
	for(int i = 0; i < m_Count; i++)
	{
		Graphics()->UnloadTexture(&(m_aTextures[i]));
		m_aTextureUsedByTileOrQuadLayerFlag[i] = 0;
	}
	m_Count = 0;

	int Start;
	pMap->GetType(MAPITEMTYPE_IMAGE, &Start, &m_Count);

	m_Count = clamp<int>(m_Count, 0, MAX_MAPIMAGES);

	for(int g = 0; g < pLayers->NumGroups(); g++)
	{
		CMapItemGroup *pGroup = pLayers->GetGroup(g);
		if(!pGroup)
		{
			continue;
		}

		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			CMapItemLayer *pLayer = pLayers->GetLayer(pGroup->m_StartLayer + l);
			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				CMapItemLayerTilemap *pTLayer = (CMapItemLayerTilemap *)pLayer;
				if(pTLayer->m_Image >= 0 && pTLayer->m_Image < m_Count)
				{
					m_aTextureUsedByTileOrQuadLayerFlag[pTLayer->m_Image] |= 1;
				}
			}
			else if(pLayer->m_Type == LAYERTYPE_QUADS)
			{
				CMapItemLayerQuads *pQLayer = (CMapItemLayerQuads *)pLayer;
				if(pQLayer->m_Image >= 0 && pQLayer->m_Image < m_Count)
				{
					m_aTextureUsedByTileOrQuadLayerFlag[pQLayer->m_Image] |= 2;
				}
			}
		}
	}

	const int TextureLoadFlag = Graphics()->Uses2DTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE;

	// load new textures
	bool ShowWarning = false;
	for(int i = 0; i < m_Count; i++)
	{
		const int LoadFlag = (((m_aTextureUsedByTileOrQuadLayerFlag[i] & 1) != 0) ? TextureLoadFlag : 0) | (((m_aTextureUsedByTileOrQuadLayerFlag[i] & 2) != 0) ? 0 : (Graphics()->HasTextureArraysSupport() ? IGraphics::TEXLOAD_NO_2D_TEXTURE : 0));
		const CMapItemImage_v2 *pImg = (CMapItemImage_v2 *)pMap->GetItem(Start + i);
		const CImageInfo::EImageFormat Format = pImg->m_Version < CMapItemImage_v2::CURRENT_VERSION ? CImageInfo::FORMAT_RGBA : CImageInfo::ImageFormatFromInt(pImg->m_Format);

		const char *pName = pMap->GetDataString(pImg->m_ImageName);
		if(pName == nullptr || pName[0] == '\0')
		{
			if(pImg->m_External)
			{
				log_error("mapimages", "Failed to load map image %d: failed to load name.", i);
				ShowWarning = true;
				continue;
			}
			pName = "(error)";
		}

		if(pImg->m_External)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "mapres/%s.png", pName);
			m_aTextures[i] = Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL, LoadFlag);
		}
		else if(Format == CImageInfo::FORMAT_RGBA)
		{
			CImageInfo ImageInfo;
			ImageInfo.m_Width = pImg->m_Width;
			ImageInfo.m_Height = pImg->m_Height;
			ImageInfo.m_Format = Format;
			ImageInfo.m_pData = static_cast<uint8_t *>(pMap->GetData(pImg->m_ImageData));
			char aTexName[IO_MAX_PATH_LENGTH];
			str_format(aTexName, sizeof(aTexName), "embedded: %s", pName);
			m_aTextures[i] = Graphics()->LoadTextureRaw(ImageInfo, LoadFlag, aTexName);
			pMap->UnloadData(pImg->m_ImageData);
		}
		pMap->UnloadData(pImg->m_ImageName);
		ShowWarning = ShowWarning || m_aTextures[i].IsNullTexture();
	}
	if(ShowWarning)
	{
		Client()->AddWarning(SWarning(Localize("Some map images could not be loaded. Check the local console for details.")));
	}
}

void CMapImages::OnMapLoad()
{
	IMap *pMap = Kernel()->RequestInterface<IMap>();
	CLayers *pLayers = m_pClient->Layers();
	OnMapLoadImpl(pLayers, pMap);
}

void CMapImages::LoadBackground(class CLayers *pLayers, class IMap *pMap)
{
	OnMapLoadImpl(pLayers, pMap);
}

static EMapImageModType GetEntitiesModType(const CGameInfo &GameInfo)
{
	if(GameInfo.m_EntitiesFDDrace)
		return MAP_IMAGE_MOD_TYPE_FDDRACE;
	else if(GameInfo.m_EntitiesDDNet)
		return MAP_IMAGE_MOD_TYPE_DDNET;
	else if(GameInfo.m_EntitiesDDRace)
		return MAP_IMAGE_MOD_TYPE_DDRACE;
	else if(GameInfo.m_EntitiesRace)
		return MAP_IMAGE_MOD_TYPE_RACE;
	else if(GameInfo.m_EntitiesBW)
		return MAP_IMAGE_MOD_TYPE_BLOCKWORLDS;
	else if(GameInfo.m_EntitiesFNG)
		return MAP_IMAGE_MOD_TYPE_FNG;
	else if(GameInfo.m_EntitiesVanilla)
		return MAP_IMAGE_MOD_TYPE_VANILLA;
	else
		return MAP_IMAGE_MOD_TYPE_DDNET;
}

static bool IsValidTile(int LayerType, bool EntitiesAreMasked, EMapImageModType EntitiesModType, int TileIndex)
{
	if(TileIndex == TILE_AIR)
		return false;
	if(!EntitiesAreMasked)
		return true;

	if(EntitiesModType == MAP_IMAGE_MOD_TYPE_DDNET || EntitiesModType == MAP_IMAGE_MOD_TYPE_DDRACE)
	{
		if(EntitiesModType == MAP_IMAGE_MOD_TYPE_DDNET || TileIndex != TILE_BOOST)
		{
			if(LayerType == MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH &&
				!IsValidGameTile(TileIndex) &&
				!IsValidFrontTile(TileIndex) &&
				!IsValidSpeedupTile(TileIndex) &&
				!IsValidTeleTile(TileIndex) &&
				!IsValidTuneTile(TileIndex))
			{
				return false;
			}
			else if(LayerType == MAP_IMAGE_ENTITY_LAYER_TYPE_SWITCH &&
				!IsValidSwitchTile(TileIndex))
			{
				return false;
			}
		}
	}
	else if(EntitiesModType == MAP_IMAGE_MOD_TYPE_RACE && IsCreditsTile(TileIndex))
	{
		return false;
	}
	else if(EntitiesModType == MAP_IMAGE_MOD_TYPE_FNG && IsCreditsTile(TileIndex))
	{
		return false;
	}
	else if(EntitiesModType == MAP_IMAGE_MOD_TYPE_VANILLA && IsCreditsTile(TileIndex))
	{
		return false;
	}
	return true;
}

IGraphics::CTextureHandle CMapImages::GetEntities(EMapImageEntityLayerType EntityLayerType)
{
	const bool EntitiesAreMasked = !GameClient()->m_GameInfo.m_DontMaskEntities;
	const EMapImageModType EntitiesModType = GetEntitiesModType(GameClient()->m_GameInfo);

	if(!m_aEntitiesIsLoaded[(EntitiesModType * 2) + (int)EntitiesAreMasked])
	{
		m_aEntitiesIsLoaded[(EntitiesModType * 2) + (int)EntitiesAreMasked] = true;

		int TextureLoadFlag = 0;
		if(Graphics()->HasTextureArraysSupport())
			TextureLoadFlag = (Graphics()->Uses2DTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE) | IGraphics::TEXLOAD_NO_2D_TEXTURE;

		CImageInfo ImgInfo;
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "%s/%s.png", m_aEntitiesPath, gs_apModEntitiesNames[EntitiesModType]);
		Graphics()->LoadPNG(&ImgInfo, aPath, IStorage::TYPE_ALL);

		// try as single ddnet replacement
		if(ImgInfo.m_pData == nullptr && EntitiesModType == MAP_IMAGE_MOD_TYPE_DDNET)
		{
			str_format(aPath, sizeof(aPath), "%s.png", m_aEntitiesPath);
			Graphics()->LoadPNG(&ImgInfo, aPath, IStorage::TYPE_ALL);
		}

		// try default
		if(ImgInfo.m_pData == nullptr)
		{
			str_format(aPath, sizeof(aPath), "editor/entities_clear/%s.png", gs_apModEntitiesNames[EntitiesModType]);
			Graphics()->LoadPNG(&ImgInfo, aPath, IStorage::TYPE_ALL);
		}

		if(ImgInfo.m_pData != nullptr)
		{
			CImageInfo BuildImageInfo;
			BuildImageInfo.m_Width = ImgInfo.m_Width;
			BuildImageInfo.m_Height = ImgInfo.m_Height;
			BuildImageInfo.m_Format = ImgInfo.m_Format;
			BuildImageInfo.m_pData = static_cast<uint8_t *>(malloc(BuildImageInfo.DataSize()));

			// build game layer
			for(int LayerType = 0; LayerType < MAP_IMAGE_ENTITY_LAYER_TYPE_COUNT; ++LayerType)
			{
				dbg_assert(!m_aaEntitiesTextures[(EntitiesModType * 2) + (int)EntitiesAreMasked][LayerType].IsValid(), "entities texture already loaded when it should not be");

				// set everything transparent
				mem_zero(BuildImageInfo.m_pData, BuildImageInfo.DataSize());

				for(int i = 0; i < 256; ++i)
				{
					int TileIndex = i;
					if(IsValidTile(LayerType, EntitiesAreMasked, EntitiesModType, TileIndex))
					{
						if(LayerType == MAP_IMAGE_ENTITY_LAYER_TYPE_SWITCH && TileIndex == TILE_SWITCHTIMEDOPEN)
						{
							TileIndex = 8;
						}

						const size_t CopyWidth = ImgInfo.m_Width / 16;
						const size_t CopyHeight = ImgInfo.m_Height / 16;
						const size_t OffsetX = (size_t)(TileIndex % 16) * CopyWidth;
						const size_t OffsetY = (size_t)(TileIndex / 16) * CopyHeight;
						Graphics()->CopyTextureBufferSub(BuildImageInfo.m_pData, ImgInfo, OffsetX, OffsetY, CopyWidth, CopyHeight);
					}
				}

				m_aaEntitiesTextures[(EntitiesModType * 2) + (int)EntitiesAreMasked][LayerType] = Graphics()->LoadTextureRaw(BuildImageInfo, TextureLoadFlag, aPath);
			}

			BuildImageInfo.Free();
			ImgInfo.Free();
		}
	}

	return m_aaEntitiesTextures[(EntitiesModType * 2) + (int)EntitiesAreMasked][EntityLayerType];
}

IGraphics::CTextureHandle CMapImages::GetSpeedupArrow()
{
	if(!m_SpeedupArrowIsLoaded)
	{
		int TextureLoadFlag = (Graphics()->Uses2DTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE) | IGraphics::TEXLOAD_NO_2D_TEXTURE;
		m_SpeedupArrowTexture = Graphics()->LoadTexture("editor/speed_arrow_array.png", IStorage::TYPE_ALL, TextureLoadFlag);
		m_SpeedupArrowIsLoaded = true;
	}
	return m_SpeedupArrowTexture;
}

IGraphics::CTextureHandle CMapImages::GetOverlayBottom()
{
	return m_OverlayBottomTexture;
}

IGraphics::CTextureHandle CMapImages::GetOverlayTop()
{
	return m_OverlayTopTexture;
}

IGraphics::CTextureHandle CMapImages::GetOverlayCenter()
{
	return m_OverlayCenterTexture;
}

void CMapImages::ChangeEntitiesPath(const char *pPath)
{
	if(str_comp(pPath, "default") == 0)
		str_copy(m_aEntitiesPath, "editor/entities_clear");
	else
	{
		str_format(m_aEntitiesPath, sizeof(m_aEntitiesPath), "assets/entities/%s", pPath);
	}

	for(int ModType = 0; ModType < MAP_IMAGE_MOD_TYPE_COUNT * 2; ++ModType)
	{
		if(m_aEntitiesIsLoaded[ModType])
		{
			for(int LayerType = 0; LayerType < MAP_IMAGE_ENTITY_LAYER_TYPE_COUNT; ++LayerType)
			{
				if(m_aaEntitiesTextures[ModType][LayerType].IsValid())
				{
					Graphics()->UnloadTexture(&(m_aaEntitiesTextures[ModType][LayerType]));
				}
				m_aaEntitiesTextures[ModType][LayerType] = IGraphics::CTextureHandle();
			}

			m_aEntitiesIsLoaded[ModType] = false;
		}
	}
}

void CMapImages::SetTextureScale(int Scale)
{
	if(m_TextureScale == Scale)
		return;

	m_TextureScale = Scale;

	if(Graphics() && m_OverlayCenterTexture.IsValid()) // check if component was initialized
	{
		// reinitialize component
		Graphics()->UnloadTexture(&m_OverlayBottomTexture);
		Graphics()->UnloadTexture(&m_OverlayTopTexture);
		Graphics()->UnloadTexture(&m_OverlayCenterTexture);

		m_OverlayBottomTexture = IGraphics::CTextureHandle();
		m_OverlayTopTexture = IGraphics::CTextureHandle();
		m_OverlayCenterTexture = IGraphics::CTextureHandle();

		InitOverlayTextures();
	}
}

int CMapImages::GetTextureScale() const
{
	return m_TextureScale;
}

IGraphics::CTextureHandle CMapImages::UploadEntityLayerText(int TextureSize, int MaxWidth, int YOffset)
{
	CImageInfo TextImage;
	TextImage.m_Width = 1024;
	TextImage.m_Height = 1024;
	TextImage.m_Format = CImageInfo::FORMAT_RGBA;
	TextImage.m_pData = static_cast<uint8_t *>(calloc(TextImage.DataSize(), sizeof(uint8_t)));

	UpdateEntityLayerText(TextImage, TextureSize, MaxWidth, YOffset, 0);
	UpdateEntityLayerText(TextImage, TextureSize, MaxWidth, YOffset, 1);
	UpdateEntityLayerText(TextImage, TextureSize, MaxWidth, YOffset, 2, 255);

	const int TextureLoadFlag = (Graphics()->Uses2DTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE) | IGraphics::TEXLOAD_NO_2D_TEXTURE;
	return Graphics()->LoadTextureRawMove(TextImage, TextureLoadFlag);
}

void CMapImages::UpdateEntityLayerText(CImageInfo &TextImage, int TextureSize, int MaxWidth, int YOffset, int NumbersPower, int MaxNumber)
{
	char aBuf[4];
	int DigitsCount = NumbersPower + 1;

	int CurrentNumber = std::pow(10, NumbersPower);

	if(MaxNumber == -1)
		MaxNumber = CurrentNumber * 10 - 1;

	str_from_int(CurrentNumber, aBuf);

	int CurrentNumberSuitableFontSize = TextRender()->AdjustFontSize(aBuf, DigitsCount, TextureSize, MaxWidth);
	int UniversalSuitableFontSize = CurrentNumberSuitableFontSize * 0.92f; // should be smoothed enough to fit any digits combination

	YOffset += ((TextureSize - UniversalSuitableFontSize) / 2);

	for(; CurrentNumber <= MaxNumber; ++CurrentNumber)
	{
		str_from_int(CurrentNumber, aBuf);

		float x = (CurrentNumber % 16) * 64;
		float y = (CurrentNumber / 16) * 64;

		int ApproximateTextWidth = TextRender()->CalculateTextWidth(aBuf, DigitsCount, 0, UniversalSuitableFontSize);
		int XOffSet = (MaxWidth - clamp(ApproximateTextWidth, 0, MaxWidth)) / 2;

		TextRender()->UploadEntityLayerText(TextImage, (TextImage.m_Width / 16) - XOffSet, (TextImage.m_Height / 16) - YOffset, aBuf, DigitsCount, x + XOffSet, y + YOffset, UniversalSuitableFontSize);
	}
}

void CMapImages::InitOverlayTextures()
{
	int TextureSize = 64 * m_TextureScale / 100;
	TextureSize = clamp(TextureSize, 2, 64);
	int TextureToVerticalCenterOffset = (64 - TextureSize) / 2; // should be used to move texture to the center of 64 pixels area

	if(!m_OverlayBottomTexture.IsValid())
	{
		m_OverlayBottomTexture = UploadEntityLayerText(TextureSize / 2, 64, 32 + TextureToVerticalCenterOffset / 2);
	}

	if(!m_OverlayTopTexture.IsValid())
	{
		m_OverlayTopTexture = UploadEntityLayerText(TextureSize / 2, 64, TextureToVerticalCenterOffset / 2);
	}

	if(!m_OverlayCenterTexture.IsValid())
	{
		m_OverlayCenterTexture = UploadEntityLayerText(TextureSize, 64, TextureToVerticalCenterOffset);
	}
}
