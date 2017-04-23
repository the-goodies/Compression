#ifndef _compression_h
#define _compression_h
#include <iomanip>
#include <thread>
#include "lib/PriorityQueue.h"


static inline void setBit(u64 & byte, u8 pos) { byte |= ((u64)1 << pos); }
static inline void setBit(u8 & byte, u8 pos) { byte |= ((u8)1 << pos); }
static inline void clearBit(u64 & byte, u8 pos) { byte &= ~((u64)1 << pos); }
static inline void clearBit(u8 & byte, u8 pos) { byte &= ~((u8)1 << pos); }
static inline bool getBit(u64 byte, u8 pos) { return (byte & ((u64)1 << pos)) != 0; }

// writes a bit to given outBuffer and advances bit_pos and byte_pos if needed
static inline void writeBit(bool bit, u8* outBuffer, s64 & byte_pos, u8 & bit_pos)
{
	u8 byte;
	if (bit_pos == 0) byte = 0;
	else			  byte = outBuffer[byte_pos];

	if (bit) byte |= (1 << bit_pos);
	outBuffer[byte_pos] = byte;

	bit_pos += 1;
	if (bit_pos == 8)
	{
		bit_pos = 0;
		byte_pos += 1;
	}
}


// writes a byte to a given outBuffer and advances byte_pos
// bit_pos is needed, since it's not necessary 0
static inline void writeByte(u8 byte, u8* outBuffer, s64 & byte_pos, u8 bit_pos)
{
	if (bit_pos == 0) outBuffer[byte_pos++] = byte;
	else
	{
		// fill the remaining of byte and overwrite the last byte in a outBuffer with it
		outBuffer[byte_pos++] |= ((byte & ((1 << (8 - bit_pos)) - 1)) << bit_pos);
		// put the rest of bits to a new byte
		outBuffer[byte_pos] = (byte >> (8 - bit_pos));
	}
}

// writes 4 bytes to a outBuffer from a given u32 value
static inline void writeFourBytes(u32 bytes, u8* outBuffer, s64 & byte_pos, u8 bit_pos)
{
	for (s32 offset = 0; offset < 4; ++offset)
	{
		u8 byte = *((u8*)&bytes + offset);
		writeByte(byte, outBuffer, byte_pos, bit_pos);
	}
}

// returns next bit within given inBuffer, true if 1 else 0, also advances bit_pos and byte_pos if needed
inline static bool readBit(u8* inBuffer, s64 & byte_pos, u8 & bit_pos)
{
	bool bit = (inBuffer[byte_pos] & (1 << bit_pos)) != 0;
	bit_pos += 1;
	if (bit_pos == 8)
	{
		bit_pos = 0;
		byte_pos += 1;
	}
	return bit;
}

// returns next byte from given inBuffer and also advances byte_pos
inline static u8 readByte(u8* inBuffer, s64 & byte_pos, u8 bit_pos)
{
	if (bit_pos == 0) return inBuffer[byte_pos++];
	else
	{
		// get remaining bits from this byte
		u8 byte = inBuffer[byte_pos++] >> bit_pos;
		// and the rest from the next byte
		byte |= ((inBuffer[byte_pos] & ((1 << bit_pos) - 1)) << (8 - bit_pos));
		return byte;
	}
}

// returns next four bytes from given inBuffer and also advances byte_pos by 4
static inline u32 readFourBytes(u8* inBuffer, s64 & byte_pos, u8 bit_pos)
{
	u32 bytes = 0;
	for (s32 offset = 0; offset < 4; ++offset)
	{
		u8 byte = readByte(inBuffer, byte_pos, bit_pos);
		bytes |= byte << (8*offset);
	}
	return bytes;
}




struct HuffmanNode
{
	u8 symbol;
	s32 freq;
	HuffmanNode* zero;
	HuffmanNode* one;

	HuffmanNode(u8 s = 0, s32 freq = 0, HuffmanNode* zero = nullptr, HuffmanNode* one = nullptr):
		symbol(s), freq(freq), zero(zero), one(one) { /* empty */ }

	bool isLeaf() const { return zero == nullptr && one == nullptr; }
};


struct ArrayNode
{
	u8 symbol;
	// virtual_pos is zero if node is a leaf, otherwise
	// based on this value real position of children in HuffmanTree array will be determined:
	// (virtual_pos * 2) and (virtual_pos * 2 + 1) actual positions respectively for children of this node
	// in array, virtual_pos is increased when passing to the next child only if it is not a leaf
	u16 virtual_pos;

	bool isLeaf() { return virtual_pos == 0; }
	ArrayNode(): symbol(0), virtual_pos(0) { /* empty */ }
	ArrayNode(u8 s, u16 virtual_pos): symbol(s), virtual_pos(virtual_pos) { /* empty */ }
};


static ArrayNode HuffmanArray[512];
// optimization so that when decompressing and using huffmanTree in a tight loop
// array will be used instead due to cache misses of typical binary tree
// (after testing both): 4x speed improvement using HuffmanArray instead of typical binary tree
static u16 transferHuffmanTreeToArray(HuffmanNode* tree, u16 real_pos = 1, u16 virtual_pos = 1)
{
	HuffmanArray[real_pos] = ArrayNode(tree->symbol, virtual_pos);
	// no need to check the other side, since every node is either a leaf or have both children
	if (tree->zero == nullptr)
	{
		HuffmanArray[real_pos].virtual_pos = 0;
		return virtual_pos;
	}
	return transferHuffmanTreeToArray(tree->one, virtual_pos * 2 + 1, 
			transferHuffmanTreeToArray(tree->zero, virtual_pos * 2, virtual_pos + 1) );
}

// comparison function so that priorityQueue can compare huffman nodes
struct compare_huffmanNodes { bool operator()(HuffmanNode* lhs, HuffmanNode* rhs) { return lhs->freq < rhs->freq; } };

static void clearTree(HuffmanNode* tree)
{
	if (tree == nullptr) return;
	clearTree(tree->zero);
	clearTree(tree->one);
	delete tree;
}

// freqTable size should be 256 -> one position to store freq for each byte
static void buildFrequencyTable(u8* inBuffer, s64 inBuffer_size, s32* freqTable)
{
	for (s64 i = 0; i < 256; ++i) freqTable[i] = 0;
	for (s64 i = 0; i < inBuffer_size; ++i) freqTable[inBuffer[i]] += 1;
}

static HuffmanNode* buildHuffmanTree(s32* freqTable)
{
	// fill HuffmanForest with symbols from freqTable
	PriorityQueue<HuffmanNode*, compare_huffmanNodes> HuffmanForest;
	for (s32 s = 0; s < 256; ++s)
		if (freqTable[s] > 0)
			HuffmanForest.insert(new HuffmanNode((u8)s, freqTable[s]));

	// special case if HuffmanForest only has one element
	// need to add another random one, which will not be used anyway
	// so that the correct tree could be build
	if (HuffmanForest.size() == 1)
	{
		if (freqTable[0] != 0) HuffmanForest.insert(new HuffmanNode(1));
		else HuffmanForest.insert(new HuffmanNode(0));
	}

	while (HuffmanForest.size() > 1)
	{
		HuffmanNode* zero = HuffmanForest.get();
		HuffmanNode* one = HuffmanForest.get();
		HuffmanNode* parent = new HuffmanNode(0, zero->freq + one->freq, zero, one);
		HuffmanForest.insert(parent);
	}
	return HuffmanForest.get();
}

// writes tree to a given outBuffer as a header
// so that receiver later could use it to expand said buffer
static void writeHuffmanTree(HuffmanNode* tree, u8* outBuffer, s64 & byte_pos, u8 & bit_pos)
{
	if (tree->isLeaf())
	{
		writeBit(true, outBuffer, byte_pos, bit_pos); // write one
		writeByte(tree->symbol, outBuffer, byte_pos, bit_pos); // then symbol
		return;
	}

	// otherwise it's internal node, write zero and
	// recursively write other subtree's info to a outBuffer
	writeBit(false, outBuffer, byte_pos, bit_pos);
	writeHuffmanTree(tree->zero, outBuffer, byte_pos, bit_pos);
	writeHuffmanTree(tree->one, outBuffer, byte_pos, bit_pos);
}

static HuffmanNode* readHuffmanTree(u8* inBuffer, s64 & byte_pos, u8 & bit_pos)
{
	// base case, bit one indicates a leaf, so next byte is a symbol
	if (readBit(inBuffer, byte_pos, bit_pos) == 1) return new HuffmanNode(readByte(inBuffer, byte_pos, bit_pos));

	// otherwise consumed bit was zero, so node is internal
	// recursively descend to get other subtree's info
	HuffmanNode* zero = readHuffmanTree(inBuffer, byte_pos, bit_pos);
	HuffmanNode* one = readHuffmanTree(inBuffer, byte_pos, bit_pos);
	return new HuffmanNode(0, 0, zero, one);
}


struct codeword
{
	u8 code[32] = {};
	// indicates valid bits upto limit index in code[32], limit bit not included
	u8 limit = 0;
};

// makes a symbol table maping symbols to codewords: used for encoding a file
static void buildEncodingMap(HuffmanNode* tree, codeword st[256], u8 code[32], u8 limit = 0)
{
	// base case
	if (tree->isLeaf())
	{
		for (u8 i = 0; i < 32; ++i)
			st[tree->symbol].code[i] = code[i];
		st[tree->symbol].limit = limit;
		return;
	}
	// recursive step traversing tree
	clearBit(code[limit / 8], limit % 8);
	buildEncodingMap(tree->zero, st, code, limit + 1);

	setBit(code[limit / 8], limit % 8);
	buildEncodingMap(tree->one, st, code, limit + 1);
}

struct filenames
{
	char* inFileName;
	char* outFileName;
};

// statistics for printing info while computing or after
struct statistics
{
	// track progress
	s64* byte_pos_in;
	s64* byte_pos_out;
	// after byte_pos reaches this point, computing is almost over
	s64 byte_pos_last;
	filenames files;
};


// print to console info on compression/decompression progress while computing
static void printProgressBar(statistics stats, bool compressing)
{
	float inFile_size_bytes = (float)stats.byte_pos_last;
	char byte_prefix[5] = ""; // mega, kilo or just bytes
	if (inFile_size_bytes > 1024 * 1024)
	{
		strcpy_s(byte_prefix, 5, "mega");
		inFile_size_bytes /= (1024 * 1024);
	}
	else if (inFile_size_bytes > 1024)
	{
		strcpy_s(byte_prefix, 5, "kilo");
		inFile_size_bytes /= 1024;
	}

	std::cout << (compressing ? "Suspaudžia" : "Isskleidžia") << " failą " << stats.files.inFileName << " (" << 
		std::setprecision(stats.byte_pos_last <= 1024 ? 0 : 3) << inFile_size_bytes << " " << byte_prefix << "baitai)\n";

	// progress bar and percentage printing
	while (*(stats.byte_pos_in) + 1 < stats.byte_pos_last)
	{
		float percent = *(stats.byte_pos_in) / (float)stats.byte_pos_last;
		std::cout << "[";
		for (int progress = 0; progress < 100; progress += 5)
		{
			if (progress < percent * 100) std::cout << "|";
			else						  std::cout << " ";
		}
		std::cout << "] " << std::fixed << std::setw(5) << std::setprecision(2) << percent * 100 << "% ";
		std::cout << "\r";
		std::cout.flush();
	}
	std::cout << "[";
	for (int progress = 0; progress < 100; progress += 5) std::cout << "|";
	std::cout << "] " << std::fixed << std::setw(5) << std::setprecision(2) << 100.0f << "% \n";


	// print success and final bytes of output file
	char operation[30];
	if (compressing) strcpy_s(operation, 30, " sėkmingai suspaustas į ");
	else										  strcpy_s(operation, 30, " sėkmingai isskleistas į ");

	float outFile_size_bytes = (float)*(stats.byte_pos_out);
	char out_byte_prefix[5] = "";
	if (outFile_size_bytes > 1024 * 1024)
	{
		strcpy_s(out_byte_prefix, 5, "mega");
		outFile_size_bytes /= (1024 * 1024);
	}
	else if (outFile_size_bytes > 1024)
	{
		strcpy_s(out_byte_prefix, 5, "kilo");
		outFile_size_bytes /= 1024;
	}
	std::cout << stats.files.inFileName << operation << stats.files.outFileName << " (" << 
		std::setprecision((float)*stats.byte_pos_out <= 1024 ? 0 : 3) << outFile_size_bytes << " " << out_byte_prefix << "baitai)\n";

	std::cout.flush();
}

// compresses file from inBuffer to outBuffer and returns size of compressed size in bytes
s64 compress(u8* inBuffer, const s64 inBuffer_size, u8* outBuffer, s64 outBuffer_size, filenames files)
{
	s32 freqTable[256] = {};
	buildFrequencyTable(inBuffer, inBuffer_size, freqTable);

	HuffmanNode* root = buildHuffmanTree(freqTable);

	codeword st[256]; // symbol table maping symbols to codewords
	u8 temp_code[32] = {};
	buildEncodingMap(root, st, temp_code, 0);

	s64 byte_pos_out = 0;
	u8 bit_pos_out = 0;
	// write a tree in order for a decoder to be able to expand
	writeHuffmanTree(root, outBuffer, byte_pos_out, bit_pos_out);

	// release huffman tree, since we dont need it anymore
	clearTree(root);

	// write ammount of symbols overall in a file
	// so decoder will know when to stop reading
	writeFourBytes((u32)inBuffer_size, outBuffer, byte_pos_out, bit_pos_out);


	s64 byte_pos_in = 0;
	// create a thread to print progress bar
	std::thread stats_thread(printProgressBar, statistics {&byte_pos_in, &byte_pos_out,
							inBuffer_size, files}, true /* compressing */);

	// use symbol table maping to encode a file
	for (s64 i = 0; i < inBuffer_size; ++i)
	{
		// readByte
		codeword code = st[inBuffer[byte_pos_in++]];

		const u8 code_byte_count = code.limit / 8;
		for (u8 code_byte_pos = 0; code_byte_pos < code_byte_count; ++code_byte_pos)
		{
			// fill the remaining of byte and overwrite the last byte in a outBuffer with it
			outBuffer[byte_pos_out++] |= ((code.code[code_byte_pos] & ((1 << (8 - bit_pos_out)) - 1)) << bit_pos_out);
			// put the rest of bits to a new byte
			outBuffer[byte_pos_out] = (code.code[code_byte_pos] >> ((8 - bit_pos_out) % 8));
		}

		//u8 byte = outBuffer[byte_pos_out];
		const u8 code_bit_remainder = code.limit % 8;
		for (u8 code_bit_pos = 0; code_bit_pos < code_bit_remainder; ++code_bit_pos)
		{
			// writeBit
			bool bit = (code.code[code_byte_count] & (1 << code_bit_pos)) != 0;
			outBuffer[byte_pos_out] |= (bit << bit_pos_out);

			bit_pos_out = (bit_pos_out + 1) % 8;
			byte_pos_out += (bit_pos_out != 0);
		}
	}
	stats_thread.join();
	return (bit_pos_out == 0 ? byte_pos_out : byte_pos_out + 1);
}

// decompress file from inBuffer to outBuffer and returns size of decompressed size in bytes
s64 decompress(u8* inBuffer, s64 inBuffer_size, u8* outBuffer, s64 outBuffer_size, filenames files)
{
	s64 byte_pos_in = 0;
	u8 bit_pos_in = 0;
	// extract huffman tree from an encoded stream
	HuffmanNode* root = readHuffmanTree(inBuffer, byte_pos_in, bit_pos_in);
	transferHuffmanTreeToArray(root);
	// get how many symbols are in an encoded stream
	u32 size = readFourBytes(inBuffer, byte_pos_in, bit_pos_in);

	s64 byte_pos_out = 0;
	// create a thread to print progress bar
	std::thread stats_thread(printProgressBar, statistics{&byte_pos_in, &byte_pos_out,
							inBuffer_size, files}, false /* compressing */);
	// decode encoded stream
	u8 byte = inBuffer[byte_pos_in];
	for (u32 pos = 0; pos < size; ++pos)
	{
		u16 real_pos = 1;
		while (!HuffmanArray[real_pos].isLeaf())
		{
			// getBit
			bool bit = (byte & (1 << bit_pos_in)) != 0;
			bit_pos_in += 1;
			if (bit_pos_in == 8)
			{
				bit_pos_in = 0;
				byte = inBuffer[++byte_pos_in];
			}

			if (bit) real_pos = HuffmanArray[real_pos].virtual_pos * 2 + 1;
			else	 real_pos = HuffmanArray[real_pos].virtual_pos * 2;
		}
		// writeByte
		outBuffer[byte_pos_out++] = HuffmanArray[real_pos].symbol;
	}
	stats_thread.join();
	return byte_pos_out;
}

#endif

