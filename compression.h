#ifndef _compression_h
#define _compression_h

#include <string>
#include "lib/PriorityQueue.h"
#include "lib/bitstream.h"
#include "lib/utility.h"


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

// comparison function so that priorityQueue can compare huffman nodes
struct compare_huffmanNodes { bool operator()(HuffmanNode* lhs, HuffmanNode* rhs) { return lhs->freq < rhs->freq; } };

// freqTable size should be 256 -> one position to store freq for each byte
void buildFrequencyTable(ifbitstream & stream, s32* freqTable)
{
	for (s32 s = 0; s < 256; ++s) freqTable[s] = 0;

	s32 s = 0;
	while ((s = stream.readByte()) != EOF)
		freqTable[s] += 1;

	stream.rewind(); // get stream to start position, so we can read again
}


HuffmanNode* buildHuffmanTree(s32* freqTable)
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

// writes huffmanTree to a compressed file as a header
// so that receiver could use it to expand said file
void writeHuffmanTree(ofbitstream & stream, HuffmanNode* HuffmanTree)
{
	// base case, when geting to a leaf, write one and a byte
	if (HuffmanTree->isLeaf())
	{
		stream.writeBit(true); // write one
		stream.writeByte(HuffmanTree->symbol); // then symbol
		return;
	}

	// otherwise it's internal node, write zero and
	// recursively write other subtree's info to a stream
	stream.writeBit(false);
	writeHuffmanTree(stream, HuffmanTree->zero);
	writeHuffmanTree(stream, HuffmanTree->one);
}

// reads and returns encoded huffman tree from a header of given 
// compressed file to use it to expand said file
HuffmanNode* readHuffmanTree(ifbitstream & stream)
{
	// base case, bit one indicates a leaf, so next byte is a symbol
	if (stream.readBit() == 1) return new HuffmanNode(stream.readByte());

	// otherwise consumed bit was zero, so node is internal
	// recursively descend to get other subtree's info
	HuffmanNode* zero = readHuffmanTree(stream);
	HuffmanNode* one = readHuffmanTree(stream);
	return new HuffmanNode(0, 0, zero, one);
}


// makes a symbol table maping symbols to codewords: used for encoding a file
void buildEncodingMap(HuffmanNode* HuffmanTree, std::string st[], std::string codeword = "")
{
	// base case
	if (HuffmanTree->isLeaf())
	{
		st[HuffmanTree->symbol] = codeword;
		return;
	}
	// recursive step traversing tree
	buildEncodingMap(HuffmanTree->zero, st, codeword + "0");
	buildEncodingMap(HuffmanTree->one, st, codeword + "1");
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



#endif