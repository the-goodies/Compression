#ifndef _compression_h
#define _compression_h

#include <string>
#include "lib/PriorityQueue.h"
//#include "lib/bitstream.h"
#include "lib/utility.h"


// writes a bit to given outBuffer and advances bit_pos and byte_pos if needed
static void writeBit(bool bit, u8* outBuffer, s64 & byte_pos, u8 & bit_pos)
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

static inline void setBit(u64 & byte, u8 pos) { byte |= (1 << pos); }
static inline void clearBit(u64 & byte, u8 pos) { byte &= ~(1 << pos); }
static inline bool getBit(u64 byte, u8 pos) { return (byte & (1 << pos)) != 0; }

// writes a byte to a given outBuffer and advances byte_pos
// bit_pos is needed, since it's not necessary 0
static void writeByte(u8 byte, u8* outBuffer, s64 & byte_pos, u8 bit_pos)
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
static void writeFourBytes(u32 bytes, u8* outBuffer, s64 & byte_pos, u8 bit_pos)
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

// returns next bit within given inBuffer, true if 1 else 0, also advances bit_pos and byte_pos if needed
inline static bool readBit(u8 byte, u8 & bit_pos)
{
	bool bit = (byte & (1 << bit_pos)) != 0;
	bit_pos += 1;
	if (bit_pos == 8) bit_pos = 0;
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
static u32 readFourBytes(u8* inBuffer, s64 & byte_pos, u8 bit_pos)
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

	ArrayNode(): symbol(0), virtual_pos(0) { /* empty */ }
	ArrayNode(u8 s, u16 virtual_pos): symbol(s), virtual_pos(virtual_pos) { /* empty */ }
};


static ArrayNode HuffmanArray[512];
// optimization so that when decompressing and using huffmanTree in a tight loop
// array will be used instread due to cache misses of typical binary tree
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
			HuffmanForest.insert(new HuffmanNode(s, freqTable[s]));

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
	u64 code[4] = {};
	// indicates valid bits upto limit index in code[4], limit bit not included
	u8 limit = 0;
};

// makes a symbol table maping symbols to codewords: used for encoding a file
static void buildEncodingMap(HuffmanNode* tree, codeword st[256], u64 code[4], u8 limit)
{
	// base case
	if (tree->isLeaf())
	{
		st[tree->symbol].code[0] = code[0];
		st[tree->symbol].code[1] = code[1];
		st[tree->symbol].code[2] = code[2];
		st[tree->symbol].code[3] = code[3];
		st[tree->symbol].limit = limit;
		return;
	}
	// recursive step traversing tree
	if (limit >= 192) 		clearBit(code[3], limit);
	else if (limit >= 128) 	clearBit(code[2], limit);
	else if (limit >= 64) 	clearBit(code[1], limit);
	else					clearBit(code[0], limit);
	buildEncodingMap(tree->zero, st, code, limit + 1);

	if (limit >= 192) 		setBit(code[3], limit);
	else if (limit >= 128) 	setBit(code[2], limit);
	else if (limit >= 64) 	setBit(code[1], limit);
	else					setBit(code[0], limit);
	buildEncodingMap(tree->one, st, code, limit + 1);
}

// compresses file from inBuffer to outBuffer and returns size of compressed size in bytes
s64 compress(u8* inBuffer, s64 inBuffer_size, u8* outBuffer, s64 outBuffer_size)
{
	s32 freqTable[256] = {};
	buildFrequencyTable(inBuffer, inBuffer_size, freqTable);

	HuffmanNode* root = buildHuffmanTree(freqTable);

	codeword st[256]; // symbol table maping symbols to codewords
	u64 code[4];
	buildEncodingMap(root, st, code, 0);

	s64 out_byte_pos = 0;
	u8 out_bit_pos = 0;
	// write a tree in order for a decoder to be able to expand
	writeHuffmanTree(root, outBuffer, out_byte_pos, out_bit_pos);

	// release huffman tree, since we dont need it anymore
	clearTree(root);

	// write ammount of symbols overall in a file
	// so decoder will know when to stop reading
	u32 size = (u32)inBuffer_size;
	writeFourBytes(size, outBuffer, out_byte_pos, out_bit_pos);


	s64 in_byte_pos = 0;
	u8 in_bit_pos = 0;
	// use symbol table maping to encode a file
	for (u32 i = 0; i < size; ++i)
	{
		codeword code = st[readByte(inBuffer, in_byte_pos, in_bit_pos)];
		const u8 code_size = code.limit;
		for (u8 j = 0; j < code_size; ++j)
		{
			bool bit;
			if (j >= 192) 		bit = getBit(code.code[3], j % 64);
			else if (j >= 128) 	bit = getBit(code.code[2], j % 64);
			else if (j >= 64) 	bit = getBit(code.code[1], j % 64);
			else				bit = getBit(code.code[0], j % 64);
			writeBit(bit, outBuffer, out_byte_pos, out_bit_pos);
		}				
	}
	return (out_bit_pos == 0 ? out_byte_pos : out_byte_pos + 1);
}

// decompress file from inBuffer to outBuffer and returns size of decompressed size in bytes
s64 decompress(u8* inBuffer, s64 inBuffer_size, u8* outBuffer, s64 outBuffer_size)
{
	s64 in_byte_pos = 0;
	u8 in_bit_pos = 0;
	// extract huffman tree from an encoded stream
	HuffmanNode* root = readHuffmanTree(inBuffer, in_byte_pos, in_bit_pos);
	transferHuffmanTreeToArray(root);
	// get how many symbols are in an encoded stream
	u32 size = readFourBytes(inBuffer, in_byte_pos, in_bit_pos);

	s64 out_byte_pos = 0;
	u8 out_bit_pos = 0;
	u8 byte = 0;
	bool first = true;
	// decode encoded stream
	for (u32 pos = 0; pos < size; ++pos)
	{
		u16 index = 1;
		while (HuffmanArray[index].virtual_pos != 0)
		{
			if (first || in_bit_pos == 0)
			{
				byte = inBuffer[in_byte_pos];
				first = false;
			}
			
			bool bit = (byte & (1 << in_bit_pos)) != 0;
			in_bit_pos += 1;
			if (in_bit_pos == 8)
			{
				in_bit_pos = 0;
				in_byte_pos += 1;
			}

			if (bit)
				index = HuffmanArray[index].virtual_pos * 2 + 1;
			else
				index = HuffmanArray[index].virtual_pos * 2;
		}
		writeByte(HuffmanArray[index].symbol, outBuffer, out_byte_pos, out_bit_pos);
	}
	return (out_bit_pos == 0 ? out_byte_pos : out_byte_pos + 1);
}




/*

// freqTable size should be 256 -> one position to store freq for each byte
static void buildFrequencyTable(ifbitstream & stream, s32* freqTable)
{
	for (s32 s = 0; s < 256; ++s) freqTable[s] = 0;

	s32 s = 0;
	while ((s = stream.readByte()) != EOF)
		freqTable[s] += 1;

	stream.rewind(); // get stream to start position, so we can read again
}



// writes tree to a compressed file as a header
// so that receiver could use it to expand said file
static void writeHuffmanTree(ofbitstream & stream, HuffmanNode* tree)
{
	// base case, when geting to a leaf, write one and a byte
	if (tree->isLeaf())
	{
		stream.writeBit(true); // write one
		stream.writeByte(tree->symbol); // then symbol
		return;
	}

	// otherwise it's internal node, write zero and
	// recursively write other subtree's info to a stream
	stream.writeBit(false);
	writeHuffmanTree(stream, tree->zero);
	writeHuffmanTree(stream, tree->one);
}


// reads and returns encoded huffman tree from a header of given 
// compressed file to use it to expand said file
static HuffmanNode* readHuffmanTree(ifbitstream & stream)
{
	// base case, bit one indicates a leaf, so next byte is a symbol
	if (stream.readBit() == 1) return new HuffmanNode(stream.readByte());

	// otherwise consumed bit was zero, so node is internal
	// recursively descend to get other subtree's info
	HuffmanNode* zero = readHuffmanTree(stream);
	HuffmanNode* one = readHuffmanTree(stream);
	return new HuffmanNode(0, 0, zero, one);
}


// compresses file from instream to outstream
void compress(ifbitstream & instream, ofbitstream & outstream)
{
	s32 freqTable[256] = {0};
	buildFrequencyTable(instream, freqTable);

	HuffmanNode* root = buildHuffmanTree(freqTable);

	std::string st[256]; // symbol table maping symbols to codewords
	buildEncodingMap(root, st);

	// write a tree in order for a decoder to be able to expand
	writeHuffmanTree(outstream, root);

	// release huffman tree, since we dont need it anymore
	clearTree(root);

	// write ammount of symbols overall in a file
	// so decoder will know when to stop reading
	u32 size = instream.size();
	outstream.writeFourBytes(size);

	// use symbol table maping to encode a file
	for (u32 i = 0; i < size; ++i)
	{
		std::string code = st[instream.readByte()];
		for (u32 j = 0; j < code.size(); ++j)
		{
			if (code[j] == '1') outstream.writeBit(true);
			else outstream.writeBit(false);
		}
	}
	instream.close();
	outstream.close();
}

// decompress file from instream to outstream
void decompress(ifbitstream & instream, ofbitstream & outstream)
{
	// extract huffman tree from an encoded stream
	HuffmanNode* root = readHuffmanTree(instream);
	// get how many symbols are in an encoded stream
	s64 size = instream.readFourBytes();
	// decode encoded stream
	for (s64 pos = 0; pos < size; ++pos)
	{
		HuffmanNode* tree = root;
		while(!tree->isLeaf())
		{
			if (instream.readBit())
				tree = tree->one;
			else
				tree = tree->zero;
		}
		outstream.writeByte(tree->symbol);
	}
	instream.close();
	outstream.close();
}

*/

#endif