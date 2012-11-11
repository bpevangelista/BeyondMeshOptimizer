#include "Foundation\efwPlatform.h"
#include "Foundation\efwMemory.h"
#include "Foundation\efwConsole.h"
#include "Foundation\efwPathHelper.h"
#include "Graphics\efwUnprocessedTriMeshHelper.h"
#include "Graphics\efwWavefrontObjReader.h"
#include "Graphics\efwTextureReader.h"

#include <stdio.h>
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <set>

using namespace efw;
using namespace efw::Graphics;

//#using <System.dll>
// To use CLR I had to change
// Minimal Rebuild Yes, PDB Edit and Continue, C++ Excepitions EHsc, Basic runtime check Both

void showUsage()
{
	// TODO
}


//namespace TangentFrameCompressions 
//{
//	enum TangentFrameCompression
//	{
//		k32bTangentWithBitangent,
//		k32bQuaternion
//	};
//}

// Package format
/*
	struct UnprocessedTriMeshVertexAttribute
	{
		uint8_t attributeType;
		uint8_t componentCount;
		uint8_t offset;
	};

int32_t meshCount;

Meshes * N {
		int32_t materialId;

		int32_t vertexStride;
		int32_t vertexCount;
		int32_t indexCount;
		void* vertexData;
		void* indexData;

		// position 8B, uv (4B per UV), tangent_frame 4B, color 4B
}

int32_t materialCount;

	struct UnprocessedMaterialTexture
	{
		int32_t width;
		int32_t height;
		int32_t pitch;
		int32_t format;
		void* data;
	};

	struct UnprocessedMaterial
	{
		UnprocessedMaterialTexture normalMapTexture;
		UnprocessedMaterialTexture diffuseTexture;
		//UnprocessedMaterialTexture specularTexture;
		//UnprocessedMaterialTexture specularPowerTexture;
	};

*/

// ----

namespace VertexFormatEvdBinary
{
	const uint8_t kUnknown = 0;
	const uint8_t kU16 = 1;
	const uint8_t kS16 = 2;
	const uint8_t kU16Norm = 3;
	const uint8_t kS16Norm = 4;
	const uint8_t kFloat32 = 5;
	const uint8_t kFloat16 = 6;
}


struct VertexAttributeEvdBinary
{
	uint8_t dataFormat;					// F16/F32/S16/U16/U8 Specials:X10Y11Z11
	uint8_t dataOffset;
	uint8_t componentCount;				// 1/2/3/4
	uint8_t inputBufferSlot;			// 0..15
};


struct MeshEvdBinary
{
	//Guid materialGuid;
	uint64_t hash64;
	uint64_t materialHash64;

	VertexAttributeEvdBinary vertexAttributes[16];
	uint16_t vertexAttributeCount;
	uint8_t _pad0[2];

	uint16_t vertexStride;
	uint16_t vertexCount;
	uint32_t indexCount;

	float positionScaleBias[6];
	float uvScaleBias[4];
};
// Mesh size must be a multiple of 8. Add padding if necessary
EFW_STATIC_ASSERT(sizeof(MeshEvdBinary) % 8 == 0);


struct ModelEvdBinary
{
	int32_t meshCount;
	uint8_t _pad0[4];

	MeshEvdBinary meshes[0];
};


struct TextureEvdBinary
{
	uint16_t width;
	uint16_t height;
	uint16_t mipCount;
	uint16_t format;
	uint32_t sizeInBytes;
};


struct MaterialEvdBinary
{
	//Guid guid;
	uint64_t hash64;

	TextureEvdBinary albedoTexture;
	TextureEvdBinary normalMapTexture;
	float roughness;
	float fresnel0[3];
};


struct MaterialLibEvdBinary
{
	int32_t materialCount;
	MaterialEvdBinary materials[0];
};

// ----


void WriteModelDescToJson(const char* outputTextFilePath, const char* outputBinaryFilePath, UnprocessedTriModel* model, std::set<int32_t> skipSet)
{
	FILE* binaryFile = fopen(outputBinaryFilePath, "wb");
	FILE* textFile = fopen(outputTextFilePath, "wt");
	if (binaryFile == NULL || textFile == NULL)
	{
		// TODO Handle error
		return;
	}

	const char kJsonBegin[] = "{\n\"id\":\"EfwPkg\",\n\"v\":\"0.9.0\",\n\"meshes\":{\n";
	const char kJsonEnd[] = "}\n\"materials\":{\n}\n}";

	// Start
	int32_t maxMeshcount = model->meshCount;
	fwrite(kJsonBegin, sizeof(char), strlen(kJsonBegin), textFile);

	const int32_t kMaxBufferSize = 1 * 1024 * 1024;
	const int32_t kFlushSize = kMaxBufferSize - 1024;

	int32_t bufferIndex = 0;
	char* buffer = (char*)memalign(16, kMaxBufferSize * sizeof(char));
	memset(buffer, 0, sizeof(char) * kMaxBufferSize);

	// Note that the total mesh count may be less than model->meshCount because of the skipSet
	int32_t maxModelBinarySize = sizeof(ModelEvdBinary) + maxMeshcount * sizeof(MeshEvdBinary);
	ModelEvdBinary* modelBinaryPtr = (ModelEvdBinary*)memalign(16, maxModelBinarySize);
	memset(modelBinaryPtr, 0, maxModelBinarySize);
	
	ModelEvdBinary& modelBinary = *modelBinaryPtr;

	// Mesh
	for (uint32_t i=0; i<model->meshCount; ++i)
	{
		if (skipSet.find(i) != skipSet.end())
			continue;

		UnprocessedTriMesh& mesh = model->meshes[i];
		
		if (bufferIndex > kFlushSize)
		{
			fwrite(buffer, sizeof(char), bufferIndex, textFile);
			memset(buffer, 0, sizeof(char) * kMaxBufferSize);
			bufferIndex = 0;
		}

		// Custom data
		const int32_t kCustomUserDataCount = 10;
		float* userDataAsFloat = (float*)mesh.customUserData;
		char userValuesStr[kCustomUserDataCount][512];
		memset(userValuesStr, 0, kCustomUserDataCount * 512);

		if (mesh.customUserData != NULL)
		{
			for (int32_t j=0; j<kCustomUserDataCount; ++j)
			{
				if (userDataAsFloat[j] == ((int32_t)userDataAsFloat[j]) )
					sprintf(userValuesStr[j], "%d", ((int32_t)userDataAsFloat[j]));
				else if ( Math::Abs(userDataAsFloat[j]) < 10.0f)
					sprintf(userValuesStr[j], "%.4f", userDataAsFloat[j]);
				else if ( Math::Abs(userDataAsFloat[j]) >= 1000.0f)
					sprintf(userValuesStr[j], "%4.2f", userDataAsFloat[j]);
				else 
					sprintf(userValuesStr[j], "%3.3f", userDataAsFloat[j]);
			}
		}

		int32_t writtenBytes = sprintf(&buffer[bufferIndex],
			"\"%s\":{"
			"\"mat\":%s,"
			"\"vformat\":%d,"
			"\"vcount\":%d,"
			"\"icount\":%d",
			
			mesh.guid.GetName(),
			mesh.materialGuid.GetName(),
			
			// TODO: HACK: TODOBE Improve
			// Position, tangentFrame, color0, color1, uv0, uv1, uv2, uv3
			((1 << 0) | (3 << 1) | (0 << 3) | (0 << 4) | (1 << 5) | (0 << 6) | (0 << 7) | (0 << 8) ),
			
			mesh.vertexCount,
			mesh.indexCount);
		if (writtenBytes > 0)
			bufferIndex += writtenBytes;

		//
		//int value1 = sizeof(MeshEvdBinary);
		MeshEvdBinary& meshBinary = modelBinary.meshes[modelBinary.meshCount];
		modelBinary.meshCount++;

		meshBinary.hash64 = mesh.guid.hash64;
		meshBinary.materialHash64 = mesh.materialGuid.hash64;
		
		meshBinary.vertexAttributeCount = 3;
		meshBinary.vertexAttributes[0].dataFormat = VertexFormatEvdBinary::kU16Norm;
		meshBinary.vertexAttributes[0].dataOffset = 0;
		meshBinary.vertexAttributes[0].componentCount = 3;
		meshBinary.vertexAttributes[0].inputBufferSlot = 0;
		
		meshBinary.vertexAttributes[1].dataFormat = VertexFormatEvdBinary::kU16Norm;
		meshBinary.vertexAttributes[1].dataOffset = 3 * sizeof(uint16_t);
		meshBinary.vertexAttributes[1].componentCount = 2;
		meshBinary.vertexAttributes[1].inputBufferSlot = 1;

		meshBinary.vertexAttributes[2].dataFormat = VertexFormatEvdBinary::kU16Norm;
		meshBinary.vertexAttributes[2].dataOffset = (3+2) * sizeof(uint16_t);
		meshBinary.vertexAttributes[2].componentCount = 2;
		meshBinary.vertexAttributes[2].inputBufferSlot = 2;

		meshBinary.vertexStride = (3+2+2) * sizeof(uint16_t);
		meshBinary.vertexCount = mesh.vertexCount;

		meshBinary.indexCount = mesh.indexCount;

		if (mesh.customUserData != NULL)
		{
			memcpy(&meshBinary.positionScaleBias, &userDataAsFloat[0], sizeof(float) * 6);
			memcpy(&meshBinary.uvScaleBias, &userDataAsFloat[6], sizeof(float) * 4);

			writtenBytes = sprintf(&buffer[bufferIndex],
				",\"posCustom\":[%s,%s,%s,%s,%s,%s],"
				"\"uvCustom\":[%s,%s,%s,%s]",
				userValuesStr[0], userValuesStr[1], userValuesStr[2], userValuesStr[3], userValuesStr[4], userValuesStr[5],
				userValuesStr[6], userValuesStr[7], userValuesStr[8], userValuesStr[9]
			);
			if (writtenBytes > 0)
				bufferIndex += writtenBytes;
		}

		if (i < (model->meshCount-1))
		{
			writtenBytes = sprintf(&buffer[bufferIndex], "},\n");
		}
		else
		{
			writtenBytes = sprintf(&buffer[bufferIndex], "}\n");
		}
		if (writtenBytes > 0)
			bufferIndex += writtenBytes;
	}

	fwrite(buffer, 1, bufferIndex, textFile);
	fwrite(kJsonEnd, 1, strlen(kJsonEnd), textFile);
	EFW_SAFE_ALIGNED_FREE(buffer);

	fwrite(modelBinaryPtr, 1, sizeof(ModelEvdBinary) + modelBinary.meshCount * sizeof(MeshEvdBinary), binaryFile);
	EFW_SAFE_ALIGNED_FREE(buffer);

	fclose(textFile);
	fclose(binaryFile);
}


void WriteMaterialDescToJson(const char* outputTextFilePath, const char* outputBinaryFilePath, UnprocessedMaterialLib* materialLib, std::set<int32_t> skipSet)
{
	if (materialLib == NULL)
		return;

	FILE* binaryFile = fopen(outputBinaryFilePath, "w");
	FILE* textFile = fopen(outputTextFilePath, "wt");
	if (binaryFile == NULL || textFile == NULL)
	{
		// TODO Handle error
		return;
	}

	//float kDefaultRefractionIndex = 1.0f;
	float kDefaultFresnel0[] = {0.95f, 0.64f, 0.54f}; // copper
	//float kDefaultFresnel0[] = {0.56f, 0.57f, 0.58f}; // iron
	
	float kDefaultRoughness = 0.2f;

	const char kJsonBegin[] = "{\n\"id\":\"EfwPkg\",\n\"v\":\"0.9.0\",\n\"meshes\":{\n}\n\"materials\":{\n";
	const char kJsonEnd[] = "}\n}";

	//
	fwrite(&materialLib->materialCount, sizeof(materialLib->materialCount), 1, binaryFile);
	fwrite(kJsonBegin, sizeof(char), strlen(kJsonBegin), textFile);

	const int32_t kMaxBufferSize = 1 * 1024 * 1024;
	const int32_t kFlushSize = kMaxBufferSize - 1024;

	int32_t bufferIndex = 0;
	char* buffer = (char*)memalign(16, kMaxBufferSize * sizeof(char));
	memset(buffer, 0, sizeof(char) * kMaxBufferSize);

	// Mesh
	for (int i=0; i<materialLib->materialCount; ++i)
	{
		bool skipMaterial = (skipSet.find(i) != skipSet.end());

		UnprocessedMaterial& material = materialLib->materials[i];
		
		if (bufferIndex > kFlushSize)
		{
			fwrite(buffer, sizeof(char), bufferIndex, textFile);
			memset(buffer, 0, sizeof(char) * kMaxBufferSize);
			bufferIndex = 0;
		}

		// Start material
		int32_t writtenBytes = 0;
		writtenBytes = sprintf(&buffer[bufferIndex], "\"%s\":{", material.guid.GetName() );
		if (writtenBytes > 0)
			bufferIndex += writtenBytes;

		//max(1, width / 4) x max(1, height / 4) x 8(DXT1) or 16(DXT2-5)
		MaterialEvdBinary materialBinary;
		memset(&materialBinary, 0, sizeof(MaterialEvdBinary));
		
		materialBinary.hash64 = material.guid.hash64;
		materialBinary.roughness = kDefaultRoughness;
		memcpy(&materialBinary.fresnel0, kDefaultFresnel0, sizeof(float) * 3);

		// Has albedo texture
		if (!skipMaterial && material.albedoTexture != NULL)
		{
			const TextureDesc& textureDesc = material.albedoTexture->desc;

			writtenBytes = sprintf(&buffer[bufferIndex],
				"\"albedoTexture\":{\"width\":%d,\"height\":%d,\"mipCount\":%d,\"format\":%d,\"size\":%d},",
				textureDesc.width,
				textureDesc.height,
				textureDesc.mipCount,
				textureDesc.format,
				(uint32_t)material.albedoTexture->dataSize
				);

			materialBinary.albedoTexture.width = material.albedoTexture->desc.width;
			materialBinary.albedoTexture.height = material.albedoTexture->desc.height;
			materialBinary.albedoTexture.mipCount = material.albedoTexture->desc.mipCount;
			materialBinary.albedoTexture.format = material.albedoTexture->desc.format;
			materialBinary.albedoTexture.sizeInBytes = (uint32_t)material.albedoTexture->dataSize;
		}
		else
		{
			// TODO May support fallback albedo color?
			writtenBytes = sprintf(&buffer[bufferIndex], "\"albedoMap\":null,");
		}
		if (writtenBytes > 0)
			bufferIndex += writtenBytes;

		// Has normal map texture
		//if (!skipMaterial && material.normalMapTexture != NULL)
		//{
		//	writtenBytes = sprintf(&buffer[bufferIndex],
		//		"\"normalMap\":{\"width\":%d,\"height\":%d,\"format\":%d},",
		//		material.normalMapTexture->width,
		//		material.normalMapTexture->height,
		//		material.normalMapTexture->format
		//		);
		//}
		//else
		{
			writtenBytes = sprintf(&buffer[bufferIndex], "\"normalTexture\":null,");
		}
		if (writtenBytes > 0)
			bufferIndex += writtenBytes;

		//
		fwrite(&materialBinary, sizeof(materialBinary), 1, binaryFile);

		writtenBytes = 
			sprintf(&buffer[bufferIndex],
			"\"fresnel0\":[%1.3f,%1.3f,%1.3f],"
			"\"roughness\":%1.3f"
			"}%s\n",
			kDefaultFresnel0[0], kDefaultFresnel0[1], kDefaultFresnel0[2],
			kDefaultRoughness,
			((i < materialLib->materialCount-1)? "," : "")
			);

		if (writtenBytes > 0)
			bufferIndex += writtenBytes;
	}
	fwrite(buffer, sizeof(char), bufferIndex, textFile);
	EFW_SAFE_ALIGNED_FREE(buffer);

	fwrite(kJsonEnd, sizeof(char), strlen(kJsonEnd), textFile);
	fclose(binaryFile);
	fclose(textFile);
}


void WriteModelDataToBinary(const char* outputFileName, UnprocessedTriModel* model, std::set<int32_t> skipSet)
{
	FILE* file = fopen(outputFileName, "wb");
	if (file == NULL)
	{
		// TODO Handle error
		return;
	}

	double ratio = 0;

	uint8_t pad[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	size_t writtenBytes = 0;
	for (uint32_t i=0; i<model->meshCount; ++i)
	{
		if (skipSet.find(i) != skipSet.end())
			continue;

		UnprocessedTriMesh& mesh = model->meshes[i];

		// Write required padding
		if (writtenBytes & (sizeof(float)-1))
		{
			int32_t padBytes = sizeof(float) - (writtenBytes & (sizeof(float)-1));
			writtenBytes += fwrite(pad, 1, padBytes, file);
		}

		writtenBytes += fwrite(mesh.vertexData, 1, mesh.vertexCount*mesh.vertexStride, file);
		
		uint16_t* newIndexData = (uint16_t*)memalign(16, mesh.indexCount * sizeof(uint16_t));
		for (uint32_t j=0; j<mesh.indexCount; ++j)
		{
			newIndexData[j] = (uint16_t)((uint32_t*)mesh.indexData)[j];
		}

		//fwrite(mesh.indexData, 1, mesh.indexCount*mesh.indexStride, file);
		writtenBytes += fwrite(newIndexData, 1, mesh.indexCount*sizeof(uint16_t), file);
		EFW_SAFE_ALIGNED_FREE(newIndexData);

		ratio += ( (double)(mesh.indexCount*sizeof(uint16_t)) / (double)(mesh.vertexCount*mesh.vertexStride/2.0) );
	}

	printf("16b-Index/16b-VertexComponents: %f\r\n", ratio/model->meshCount);

	fclose(file);
}


void WriteMaterialDataToBinary(const char* outputFileName, UnprocessedMaterialLib* materialLib, std::set<int32_t> skipSet)
{
	FILE* file = fopen(outputFileName, "wb");
	if (file == NULL)
	{
		// TODO Handle error
		return;
	}

	uint8_t pad[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	size_t totalWrittenBytes = 0;
	for (int i=0; i<materialLib->materialCount; ++i)
	{
		if (skipSet.find(i) != skipSet.end())
			continue;

		UnprocessedMaterial& material = materialLib->materials[i];

		// Write required padding
		if (totalWrittenBytes & 7)
		{
			int32_t padBytes = 8 - (totalWrittenBytes & 7);
			int32_t writtenBytes = fwrite(pad, 1, padBytes, file);
			totalWrittenBytes += writtenBytes;
		}

		// Write texture data
		if (material.albedoTexture != NULL)
		{
			const Texture& texture = *material.albedoTexture;
			int32_t writtenBytes = fwrite(texture.data, 1, (size_t)texture.dataSize, file);
			totalWrittenBytes += writtenBytes;

			EFW_ASSERT(writtenBytes == texture.dataSize);
		}

		//if (material.normalMapTexture != NULL)
		//{
		//	UnprocessedMaterialTexture& texture = *material.normalMapTexture;
		//	writtenBytes += fwrite(texture.data, 1, texture.pitch*texture.height, file);
		//}
	}

	fclose(file);
}


void ConverMaterialData(UnprocessedMaterialLib* materialLib)
{
	char currentPath[Path::kMaxDirectoryLength];
	GetCurrentDirectoryA(Path::kMaxFullPathLength, currentPath);

	for (int i=0; i<materialLib->materialCount; ++i)
	{
		UnprocessedMaterial& material = materialLib->materials[i];

		// Write texture data
		if (material.albedoTexture != NULL)
		{
			int32_t result;
			char newFileName[Path::kMaxFullPathLength];
			sprintf(newFileName, "%s.dds", material.albedoTextureFilename);

			// Use compressonator to convert texture data
			char buffer[Path::kMaxFullPathLength * 2 + 1024];
			sprintf(buffer, "%s\\dependencies\\Compressonator\\TheCompressonator.exe"
				" -convert -log -overwrite \"%s\" \"%s\" -format .dds -codec dxtc.dll +fourCC DXT1 -mipper boxfilter.dll ~MinMipSize 1",
				currentPath, material.albedoTextureFilename, newFileName);

			STARTUPINFOA info = {sizeof(info)};
			PROCESS_INFORMATION processInfo;
			result = CreateProcessA(NULL, buffer, NULL, NULL, TRUE, 0, NULL, NULL, &info, &processInfo);
			EFW_ASSERT(result != 0); // zero indicates error

			// Close process
			::WaitForSingleObject(processInfo.hProcess, INFINITE);
			CloseHandle(processInfo.hProcess);
			CloseHandle(processInfo.hThread);

			// Swap textures
			Texture* newAlbedoTexture = NULL;
			result = TextureReader::ReadDDS(&newAlbedoTexture, newFileName);
			EFW_ASSERT(result == efwErrs::kOk);
			
			TextureReader::Release(material.albedoTexture);
			material.albedoTexture = newAlbedoTexture;
		}

		//if (material.normalMapTexture != NULL)
		//{
		//	UnprocessedMaterialTexture& texture = *material.normalMapTexture;
		//	writtenBytes += fwrite(texture.data, 1, texture.pitch*texture.height, file);
		//}
	}
}


int main2(int argc, char* argv[])
{
//EFW_PACKED_TYPE( struct ZIP_Header
//{
//	uint32_t signature;
//	uint16_t version;
//	uint16_t flags;
//	uint16_t compressionType;
//	uint16_t lastModifiedTime;
//	uint16_t lastChangeDate;
//	uint32_t crc;
//	uint32_t compressedSize;
//	uint32_t uncompressedSize;
//	uint16_t filenameLength;
//	uint16_t extraFielLength;
//});
//	void* fileData;
//	int fileDataSize;
//	FileReader::ReadAll(&fileData, &fileDataSize, "sponza-meshes.zip");
//
//	ZIP_Header* header = (ZIP_Header*)fileData;
//	
//	printf("");

	uint8_t data[256];
	for (int i=0; i<256; ++i) {
		data[i] = (uint8_t)i;
	}

	FILE* file = fopen("dummy.bin", "wb");
	fwrite(data, sizeof(uint8_t), 256, file);
	fclose(file);

	return 0;
}

int main(int argc, char* argv[])
{
	//int value = sizeof(ModelEvdBinary);
	//Console::SetWriteMask(0);

	const char* kFileName = "assets\\sponza\\sponza.obj";
	//const char* kFileName = "assets\\bun_zipper_res2.obj";
	//Console::SetOutput(ConsoleOutputs::kNull);
	
	UnprocessedTriModel* model;
	UnprocessedMaterialLib* materialLib;
	WavefrontObjReader::ReadModelAndMaterials(&model, &materialLib, kFileName, FileReader::ReadAll);
	
	std::set<int32_t> modelSkipSet;
	std::set<int32_t> materialSkipSet;
	modelSkipSet.insert(3);

	// Write uncompressed models
	WriteModelDescToJson("assets_output/sponza-meshes.evd", "assets_output/sponza-meshes.evdb", model, modelSkipSet);
	WriteModelDataToBinary("assets_output/sponza-meshes.evb", model, modelSkipSet);

	// Compress mesh data
#if 1
	//for (int32_t i=0; i<model->meshCount; ++i)
	//	UnprocessedTriMeshHelper::MergeDuplicatedVertices(&model->meshes[i], 1.0f, 
	//	MergeVertexFlags::kTangentPlane_Average|MergeVertexFlags::kUvw0_Exact);
	for (uint32_t i=0; i<model->meshCount; ++i)
	{
		UnprocessedTriMesh& mesh = model->meshes[i];

		float positionScaleAndBias[6];
		float uvScaleAndBias[4];
		uint8_t* newPositionData;
		uint8_t* newNormalData;
		uint8_t* newUv0Data;

		// Compress positions and uvs
		UnprocessedTriMeshHelper::CompressVertexAttribute((void**)&newPositionData, (const float*)mesh.vertexData, mesh.vertexStride, mesh.vertexCount, 
			mesh.vertexAttributes[VertexAttributes::kPosition], AttributeCompressions::kSFloatToU16NormWithScaleAndBias, &positionScaleAndBias[0], &positionScaleAndBias[3]);
		UnprocessedTriMeshHelper::CompressVertexAttribute((void**)&newUv0Data, (const float*)mesh.vertexData, mesh.vertexStride, mesh.vertexCount, 
			mesh.vertexAttributes[VertexAttributes::kUv0], AttributeCompressions::kSFloatToU16NormWithScaleAndBias, &uvScaleAndBias[0], &uvScaleAndBias[2]);

		// 
		float* intermediateNormals;
		UnprocessedTriMeshHelper::CompressTangentSpace((void**)&intermediateNormals, (const float*)mesh.vertexData, mesh.vertexStride, mesh.vertexCount, 
			//mesh.vertexAttributes, TangentFrameCompressions::k64bNormalOnly_AzimuthalProjection);
			mesh.vertexAttributes, TangentFrameCompressions::k64bNormalOnly_SphereMapping);

		UnprocessedTriMeshVertexAttribute intermediateNormalAttribute;
		intermediateNormalAttribute.componentCount = 2;
		intermediateNormalAttribute.offset = 0;
		UnprocessedTriMeshHelper::CompressVertexAttribute((void**)&newNormalData, (const float*)intermediateNormals, 2 * sizeof(float), mesh.vertexCount, 
			intermediateNormalAttribute, AttributeCompressions::kUFloatNormToU16Norm);
		
		//UnprocessedTriMeshHelper::InterleaveVertexData(void* vertexArrays[], int32_t strides[], mesh.vertexCount);

		//
		int32_t newPositionSize = 3 * sizeof(uint16_t);
		int32_t newNormalSize = 2 * sizeof(uint16_t);
		int32_t newUv0Size = 2 * sizeof(uint16_t);
		int32_t newVertexStride = newPositionSize + newNormalSize + newUv0Size;
		uint8_t* newVertexData = (uint8_t*)memalign(16, newVertexStride*mesh.vertexCount);
		uint8_t* dataDst = (uint8_t*)newVertexData;
		
		for (uint32_t j=0; j<mesh.vertexCount; ++j)
		{
			memcpy(dataDst, newPositionData, newPositionSize);
			dataDst += newPositionSize;
			newPositionData += newPositionSize;
			
			memcpy(dataDst, newNormalData, newNormalSize);
			dataDst += newNormalSize;
			newNormalData += newNormalSize;

			memcpy(dataDst, newUv0Data, newUv0Size);
			dataDst += newUv0Size;
			newUv0Data += newUv0Size;
		}

		EFW_SAFE_ALIGNED_FREE(mesh.vertexData);
		mesh.vertexData = newVertexData;
		mesh.vertexStride = newVertexStride;

		mesh.customUserDataSize = 10 * sizeof(float);		
		mesh.customUserData = memalign(16, 10 * sizeof(float));
		memcpy(mesh.customUserData, &positionScaleAndBias[0], 6 * sizeof(float));
		memcpy( &((float*)mesh.customUserData)[6], &uvScaleAndBias[0], 4 * sizeof(float) );
	}
#endif

	// Write compressed models
	WriteModelDescToJson("assets_output/sponza-compressed3-meshes.evd", "assets_output/sponza-compressed3-meshes.evdb", model, modelSkipSet);
	WriteModelDataToBinary("assets_output/sponza-compressed3-meshes.evb", model, modelSkipSet);

	// Write material data
	WriteMaterialDescToJson("assets_output/sponza-materials.evd", "assets_output/sponza-materials.evdb", materialLib, materialSkipSet);
	WriteMaterialDataToBinary("assets_output/sponza-materials.evb", materialLib, materialSkipSet);
	
	// Compress material data
	ConverMaterialData(materialLib);
	WriteMaterialDescToJson("assets_output/sponza-compressed-materials.evd", "assets_output/sponza-compressed-materials.evdb", materialLib, materialSkipSet);
	WriteMaterialDataToBinary("assets_output/sponza-compressed-materials.evb", materialLib, materialSkipSet);
	WavefrontObjReader::Release(materialLib);
	WavefrontObjReader::Release(model);

	//EvdBi
	//FileReader::ReadAll(&



	//uint16_t* positions = NULL;
	//float* positionScales = NULL;
	//float* positionBias = NULL;

	////UnprocessedTriMeshOptimizer::SmoothNormals(); // Same normal for all vertices at the same position
	////UnprocessedTriMeshOptimizer::GenerateTangentFrame // Generate the tangent frame
	////UnprocessedTriMeshOptimizer::RemoveDuplicates(); // Remove duplicated vertices and update index list
	////UnprocessedTriMeshOptimizer::MergeDuplicatedVertices();

	////UnprocessedTriMeshOptimizer::OptimizeIndexListForPostTranformCache(); // Apply post-cache optimization

	//// Generate compressed interleaved data

	//UnprocessedTriMeshHelper::CompressSingleAttribute(&positions, &positionScales, &positionBias, model->meshes[0], 
	//	VertexAttributes::kPosition, AttributeCompressions::k16bIntegetPerComponentWithScaleAndBias);
	//
	//uint16_t* uvs = NULL;
	//
	//UnprocessedTriMeshHelper::CompressSingleAttribute(&uvs, NULL, NULL, model->meshes[0], 
	//	VertexAttributes::kUv0, AttributeCompressionTypes::k16bUintNormalize);

	//uint8_t* tangentFrames = NULL;
	//UnprocessedTriMeshHelper::CompressTangentFrame(&tangentFrames, model->meshes[0], TangentFrameCompressions::k32bTangentWithBitangent);

	//InterleavedDataDesc dataDesc;
	//dataDesc.vertexCount = model->meshes[0].vertexCount;
	//dataDesc.vertexArrayCount = 3;
	//dataDesc.vertexArra[0].data = positions;
	//dataDesc.vertexArra[0].sizePerElementInBytes = 3 * sizeof(uint16_t);
	//dataDesc.vertexArra[1].data = uvs;
	//dataDesc.vertexArra[1].sizePerElementInBytes = 2 * sizeof(uint16_t);
	//dataDesc.vertexArra[2].data = tangentFrames;
	//dataDesc.vertexArra[2].sizePerElementInBytes = sizeof(uint32_t);

	//uint8_t* vertexData = NULL;
	//UnprocessedTriMeshHelper::InterleaveData(&vertexData, &dataDesc);

	//printf("");

	return 0;
}

/*

sponza_model = {
	{
		materialId
		vertexStride;
		vertexCount;
		indexCount;
	},
	{
		materialId
		vertexStride;
		vertexCount;
		indexCount;
	},
	...

	
	}

*/