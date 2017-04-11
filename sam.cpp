/*
 * sam.cpp
 *
 *  Created on: Sep 23, 2009
 *      Author: Ben Langmead
 */

#include <vector>
#include <string>
#include <iostream>
#include "pat.h"
#include "hit.h"
#include "sam.h"

using namespace std;

/**
 * Write the SAM header lines.
 */
void SAMHitSink::appendHeaders(OutFileBuf& os,
                               size_t numRefs,
                               const vector<string>& refnames,
                               bool color,
                               bool nosq,
                               ReferenceMap *rmap,
                               const TIndexOffU* plen,
                               bool fullRef,
                               bool noQnameTrunc,
                               const char *cmdline,
                               const char *rgline)
{
	BTString o;
	o << "@HD\tVN:1.0\tSO:unsorted\n";
	if(!nosq) {
		for(size_t i = 0; i < numRefs; i++) {
			// RNAME
			o << "@SQ\tSN:";
			if(!refnames.empty() && rmap != NULL) {
				printUptoWs(o, rmap->getName(i), !fullRef);
			} else if(i < refnames.size()) {
				printUptoWs(o, refnames[i], !fullRef);
			} else {
				o << i;
			}
			o << "\tLN:" << (plen[i] + (color ? 1 : 0)) << '\n';
		}
	}
	if(rgline != NULL) {
		o << "@RG\t" << rgline << '\n';
	}
	o << "@PG\tID:Bowtie\tVN:" << BOWTIE_VERSION << "\tCL:\"" << cmdline << "\"\n";
	os.writeString(o);
}

/**
 * Append a SAM output record for an unaligned read.
 */
void SAMHitSink::appendAligned(BTString& o,
                               const Hit& h,
                               int mapq,
                               int xms, // value for XM:I field
                               const vector<string>* refnames,
                               ReferenceMap *rmap,
                               AnnotationMap *amap,
                               bool fullRef,
                               bool noQnameTrunc,
                               int offBase)
{
	// QNAME
	if(h.mate > 0) {
		// truncate final 2 chars
		for(int i = 0; i < (int)seqan::length(h.patName)-2; i++) {
			if(!noQnameTrunc && isspace((int)h.patName[i])) break;
			o << h.patName[i];
		}
	} else {
		for(int i = 0; i < (int)seqan::length(h.patName); i++) {
			if(!noQnameTrunc && isspace((int)h.patName[i])) break;
			o << h.patName[i];
		}
	}
	o << '\t';
	// FLAG
	int flags = 0;
	if(h.mate == 1) {
		flags |= SAM_FLAG_PAIRED | SAM_FLAG_FIRST_IN_PAIR | SAM_FLAG_MAPPED_PAIRED;
	} else if(h.mate == 2) {
		flags |= SAM_FLAG_PAIRED | SAM_FLAG_SECOND_IN_PAIR | SAM_FLAG_MAPPED_PAIRED;
	}
	if(!h.fw) flags |= SAM_FLAG_QUERY_STRAND;
	if(h.mate > 0 && !h.mfw) flags |= SAM_FLAG_MATE_STRAND;
	o << flags << "\t";
	// RNAME
	if(refnames != NULL && rmap != NULL) {
		printUptoWs(o, rmap->getName(h.h.first), !fullRef);
	} else if(refnames != NULL && h.h.first < refnames->size()) {
		printUptoWs(o, (*refnames)[h.h.first], !fullRef);
	} else {
		o << h.h.first;
	}
	// POS
	o << '\t' << (h.h.second + 1);
	// MAPQ
	o << '\t' << mapq;
	// CIGAR
	o << '\t' << h.length() << 'M';
	// MRNM
	if(h.mate > 0) {
		o << "\t=";
	} else {
		o << "\t*";
	}
	// MPOS
	if(h.mate > 0) {
		o << '\t' << (h.mh.second + 1);
	} else {
		o << "\t0";
	}
	// ISIZE
	o << '\t';
	if(h.mate > 0) {
		assert_eq(h.h.first, h.mh.first);
		int64_t inslen = 0;
		if(h.h.second > h.mh.second) {
			inslen = (int64_t)h.h.second - (int64_t)h.mh.second + (int64_t)h.length();
			inslen = -inslen;
		} else {
			inslen = (int64_t)h.mh.second - (int64_t)h.h.second + (int64_t)h.mlen;
		}
		o << inslen;
	} else {
		o << '0';
	}
	// SEQ
	o << '\t';
	for(size_t i = 0; i < seqan::length(h.patSeq); i++) {
		o << (char)h.patSeq[i];
	}
	// QUAL
	o << '\t';
	for(size_t i = 0; i < seqan::length(h.quals); i++) {
		o << (char)h.quals[i];
	}
	//
	// Optional fields
	//
	// Always output stratum
	o << "\tXA:i:" << (int)h.stratum;
	// Always output cost
	//ss << "\tXC:i:" << (int)h.cost;
	// Look for SNP annotations falling within the alignment
	// Output MD field
	size_t len = length(h.patSeq);
	int nm = 0;
	int run = 0;
	o << "\tMD:Z:";
	const FixedBitset<1024> *mms = &h.mms;
	ASSERT_ONLY(const String<Dna5>* pat = &h.patSeq);
	const vector<char>* refcs = &h.refcs;
#if 0
	if(h.color && false) {
		// Disabled: print MD:Z string w/r/t to colors, not letters
		mms = &h.cmms;
		ASSERT_ONLY(pat = &h.colSeq);
		assert_eq(length(h.colSeq), len+1);
		len = length(h.colSeq);
		refcs = &h.crefcs;
	}
#endif
	if(h.fw) {
		for (int i = 0; i < (int)len; ++ i) {
			if(mms->test(i)) {
				nm++;
				// There's a mismatch at this position
				assert_gt((int)refcs->size(), i);
				char refChar = toupper((*refcs)[i]);
				ASSERT_ONLY(char qryChar = (h.fw ? (*pat)[i] : (*pat)[len-i-1]));
				assert_neq(refChar, qryChar);
				o << run << refChar;
				run = 0;
			} else {
				run++;
			}
		}
	} else {
		for (int i = (int)len-1; i >= 0; -- i) {
			if(mms->test(i)) {
				nm++;
				// There's a mismatch at this position
				assert_gt((int)refcs->size(), i);
				char refChar = toupper((*refcs)[i]);
				ASSERT_ONLY(char qryChar = (h.fw ? (*pat)[i] : (*pat)[len-i-1]));
				assert_neq(refChar, qryChar);
				o << run << refChar;
				run = 0;
			} else {
				run++;
			}
		}
	}
	o << run;
	// Add optional edit distance field
	o << "\tNM:i:" << nm;
	if(h.color) {
		o << "\tCM:i:" << h.cmms.count();
	}
	// Add optional fields reporting the primer base and the downstream color,
	// which, if they were present, were clipped when the read was read in
	if(h.color && gReportColorPrimer) {
		if(h.primer != '?') {
			o << "\tZP:Z:" << h.primer;
			assert(isprint(h.primer));
		}
		if(h.trimc != '?') {
			o << "\tZp:Z:" << h.trimc;
			assert(isprint(h.trimc));
		}
	}
	if(xms > 0) {
		o << "\tXM:i:" << xms;
	}
	o << '\n';
}

/**
 * Report a verbose, human-readable alignment to the appropriate
 * output stream.
 */
void SAMHitSink::reportSamHit(BTString& o, const Hit& h, int mapq, int xms) {
	append(o, h, mapq, xms);
	{
		ThreadSafe _ts(_locks[refIdxToStreamIdx(h.h.first)]);
		if(xms == 0) {
			// Otherwise, this is actually a sampled read and belongs in
			// the same category as maxed reads
			HitSink::reportHit(o, h, false);
		}
		out(h.h.first).writeString(o);
	}
	o.clear();
}

/**
 * Report a batch of hits from a vector, perhaps subsetting it.
 */
void SAMHitSink::reportSamHits(
	BTString& o,
	vector<Hit>& hs,
	size_t start,
	size_t end,
	int mapq,
	int xms)
{
	assert_geq(end, start);
	if(end-start == 0) return;
	assert_gt(hs[start].mate, 0);
	string buf(4096, (char) 0);
	for(size_t i = start; i < end; i++) {
		append(o, hs[i], mapq, xms);
	}
	{
		ThreadSafe _ts(_locks[0]);
		out(0).writeString(o);
	}
	o.clear();
	ThreadSafe ts(&main_mutex_m);
	first_ = false;
	numAligned_++;
	numReportedPaired_ += (end-start);
}

/**
 * Report either an unaligned read or a read that exceeded the -m
 * ceiling.  We output placeholders for most of the fields in this
 * case.
 */
void SAMHitSink::reportUnOrMax(
	BTString& o,
	PatternSourcePerThread& p,
	vector<Hit>* hs,
	bool un,
	bool lock,
	size_t threadId)
{
	if(un) HitSink::reportUnaligned(o, p, threadId);
	else   HitSink::reportMaxed(o, *hs, p, threadId);
	bool paired = !p.bufb().empty();
	assert(paired || p.bufa().mate == 0);
	assert(!paired || p.bufa().mate > 0);
	assert(un || hs->size() > 0);
	assert(!un || hs == NULL || hs->size() == 0);
	size_t hssz = 0;
	if(hs != NULL) hssz = hs->size();
	if(paired) {
		// truncate final 2 chars
		for(int i = 0; i < (int)seqan::length(p.bufa().name)-2; i++) {
			if(!noQnameTrunc_ && isspace((int)p.bufa().name[i])) break;
			o << p.bufa().name[i];
		}
	} else {
		for(int i = 0; i < (int)seqan::length(p.bufa().name); i++) {
			if(!noQnameTrunc_ && isspace((int)p.bufa().name[i])) break;
			o << p.bufa().name[i];
		}
	}
	o << '\t'
	  << (SAM_FLAG_UNMAPPED | (paired ? (SAM_FLAG_PAIRED | SAM_FLAG_FIRST_IN_PAIR | SAM_FLAG_MATE_UNMAPPED) : 0)) << "\t*"
	  << "\t0\t0\t*\t*\t0\t0\t";
	for(size_t i = 0; i < seqan::length(p.bufa().patFw); i++) {
		o << (char)p.bufa().patFw[i];
	}
	o << '\t';
	for(size_t i = 0; i < seqan::length(p.bufa().qual); i++) {
		o << (char)p.bufa().qual[i];
	}
	o << "\tXM:i:" << (paired ? (hssz+1)/2 : hssz);
	// Add optional fields reporting the primer base and the downstream color,
	// which, if they were present, were clipped when the read was read in
	if(p.bufa().color && gReportColorPrimer) {
		if(p.bufa().primer != '?') {
			o << "\tZP:Z:" << p.bufa().primer;
			assert(isprint(p.bufa().primer));
		}
		if(p.bufa().trimc != '?') {
			o << "\tZp:Z:" << p.bufa().trimc;
			assert(isprint(p.bufa().trimc));
		}
	}
	o << '\n';
	if(paired) {
		// truncate final 2 chars
		for(int i = 0; i < (int)seqan::length(p.bufb().name)-2; i++) {
			o << p.bufb().name[i];
		}
		o << '\t'
		  << (SAM_FLAG_UNMAPPED | (paired ? (SAM_FLAG_PAIRED | SAM_FLAG_SECOND_IN_PAIR | SAM_FLAG_MATE_UNMAPPED) : 0)) << "\t*"
		  << "\t0\t0\t*\t*\t0\t0\t";
		for(size_t i = 0; i < seqan::length(p.bufb().patFw); i++) {
			o << (char)p.bufb().patFw[i];
		}
		o << '\t';
		for(size_t i = 0; i < seqan::length(p.bufb().qual); i++) {
			o << (char)p.bufb().qual[i];
		}
		o << "\tXM:i:" << (hssz+1)/2;
		// Add optional fields reporting the primer base and the downstream color,
		// which, if they were present, were clipped when the read was read in
		if(p.bufb().color && gReportColorPrimer) {
			if(p.bufb().primer != '?') {
				o << "\tZP:Z:" << p.bufb().primer;
				assert(isprint(p.bufb().primer));
			}
			if(p.bufb().trimc != '?') {
				o << "\tZp:Z:" << p.bufb().trimc;
				assert(isprint(p.bufb().trimc));
			}
		}
		o << '\n';
	}
	{
		ThreadSafe _ts(_locks[0], lock);
		out(0).writeString(o);
	}
	o.clear();
}

/**
 * Append a SAM alignment to the given output stream.
 */
void SAMHitSink::append(
	BTString& o,
	const Hit& h,
	int mapq,
	int xms,
	const vector<string>* refnames,
	ReferenceMap *rmap,
	AnnotationMap *amap,
	bool fullRef,
	bool noQnameTrunc,
	int offBase)
{
	appendAligned(o, h, mapq, xms, refnames, rmap, amap, fullRef, noQnameTrunc, offBase);
}

/**
 * Report maxed-out read; if sampleMax_ is set, then report 1 alignment
 * at random.
 */
void SAMHitSink::reportMaxed(
	BTString& o,
	vector<Hit>& hs,
	PatternSourcePerThread& p,
	size_t threadId)
{
	if(sampleMax_) {
		HitSink::reportMaxed(o, hs, p, threadId);
		RandomSource rand;
		rand.init(p.bufa().seed);
		assert_gt(hs.size(), 0);
		bool paired = hs.front().mate > 0;
		size_t num = 1;
		if(paired) {
			num = 0;
			int bestStratum = 999;
			for(size_t i = 0; i < hs.size()-1; i += 2) {
				int strat = min(hs[i].stratum, hs[i+1].stratum);
				if(strat < bestStratum) {
					bestStratum = strat;
					num = 1;
				} else if(strat == bestStratum) {
					num++;
				}
			}
			assert_leq(num, hs.size());
			uint32_t r = rand.nextU32() % num;
			num = 0;
			for(size_t i = 0; i < hs.size()-1; i += 2) {
				int strat = min(hs[i].stratum, hs[i+1].stratum);
				if(strat == bestStratum) {
					if(num == r) {
						reportSamHits(o, hs, i, i+2, 0, (int)(hs.size()/2)+1);
						break;
					}
					num++;
				}
			}
			assert_eq(num, r);
		} else {
			for(size_t i = 1; i < hs.size(); i++) {
				assert_geq(hs[i].stratum, hs[i-1].stratum);
				if(hs[i].stratum == hs[i-1].stratum) num++;
				else break;
			}
			assert_leq(num, hs.size());
			uint32_t r = rand.nextU32() % num;
			reportSamHit(o, hs[r], /*MAPQ*/0, /*XM:I*/(int)hs.size()+1);
		}
	} else {
		reportUnOrMax(o, p, &hs, false, threadId);
	}
}
