#include <stdlib.h>
#include <stdio.h>
#include <math.h> // For floorf
#include "md5.h"
#include "hash_ring.h"
#include <iostream>
#include <vector>
#include "MurmurHash3.h"

namespace HashRing
{

using namespace std;
using namespace v8;
using namespace node;

void MurmurHash(const void *key, int len, uint32_t seed, unsigned char out[16])
{
  if (sizeof(size_t) == 8) // 64-bit
  {
    MurmurHash3_x64_128(key, len, seed, out);
  }
  else
  {
    MurmurHash3_x86_128(key, len, seed, out);
  }
}

void HashRing::hash_digest(char *in, int len, unsigned char out[16])
{
  if (useMd5)
  {
    md5_state_t md5_state;
    md5_init(&md5_state);
    md5_append(&md5_state, (unsigned char *)in, len);
    md5_finish(&md5_state, out);
  }
  else
  {
    MurmurHash(in, len, 0, out);
  }
}

unsigned int HashRing::hash_val(char *in, int len)
{
  unsigned char digest[16];
  hash_digest(in, len, digest);
  return (unsigned int)((digest[3] << 24) |
                        (digest[2] << 16) |
                        (digest[1] << 8) |
                        digest[0]);
}

int HashRing::vpoint_compare(Vpoint *a, Vpoint *b)
{
  return (a->point < b->point) ? -1 : ((a->point > b->point) ? 1 : 0);
}

Persistent<Function> HashRing::constructor;

HashRing::HashRing(Local<Object> weight_hash, bool useMd5) : ObjectWrap(), useMd5(useMd5)
{
  Local<Array> node_names = weight_hash->GetPropertyNames();
  Local<String> node_name;
  uint32_t weight_total = 0;
  size_t num_servers = node_names->Length();

  ring.setNumServers(num_servers);
  vector<NodeInfo> &node_list = ring.node_list;
  size_t max_id_len = 0;
  // Construct the server list based on the weight hash
  for (size_t i = 0; i < num_servers; i++)
  {
    node_name = node_names->Get(i)->ToString();
    String::Utf8Value utfVal(node_name);
    size_t id_len = utfVal.length();
    if (id_len > max_id_len)
    {
      max_id_len = id_len;
    }
    node_list[i].set(*utfVal, weight_hash->Get(node_name)->Uint32Value());
    weight_total += node_list[i].weight;
  }

  int num_vpoints = 0;
  max_id_len += 50;

  vector<size_t> num_replicas;
  num_replicas.resize(num_servers);

  for (size_t j = 0; j < num_servers; j++)
  {
    float percent = (float)node_list[j].weight / (float)weight_total;
    num_replicas[j] = floorf(percent * 40.0 * (float)num_servers);
    num_vpoints += num_replicas[j] * 4;
  }

  ring.setNumPoints(num_vpoints);
  vector<Vpoint> &vpoints = ring.vpoints;

  size_t vpoint_idx = 0;
  for (size_t node_idx = 0; node_idx < num_servers; node_idx++)
  {
    for (size_t k = 0; k < num_replicas[node_idx]; k++)
    {
      char ss[max_id_len];
      int ss_len = sprintf(ss, "%s-%d", node_list[node_idx].id.c_str(), (int)k);
      unsigned char digest[16];
      hash_digest(ss, ss_len, digest);
      for (size_t m = 0; m < 4; m++)
      {
        vpoints[vpoint_idx++].set(node_idx,
                                  (digest[3 + m * 4] << 24) |
                                      (digest[2 + m * 4] << 16) |
                                      (digest[1 + m * 4] << 8) |
                                      digest[m * 4]);
      }
    }
  }

  qsort((void *)&vpoints[0], num_vpoints, sizeof(Vpoint), (compfn)vpoint_compare);
}

HashRing::~HashRing()
{
}

void HashRing::Initialize(Local<Object> exports)
{
  Isolate *isolate = exports->GetIsolate();

  Local<FunctionTemplate> tpl = FunctionTemplate::New(isolate, New);
  tpl->SetClassName(String::NewFromUtf8(isolate, "HashRing"));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  NODE_SET_PROTOTYPE_METHOD(tpl, "getNode", GetNode);

  constructor.Reset(isolate, tpl->GetFunction());

  exports->Set(String::NewFromUtf8(isolate, "HashRing"),
               tpl->GetFunction());
}

void HashRing::New(const FunctionCallbackInfo<Value> &args)
{
  Isolate *isolate = args.GetIsolate();

  if (!args.IsConstructCall())
  {
    isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Please call new HashRing(nodes)")));
  }
  else if (args[0]->IsObject())
  {
    Local<Object> weight_hash = args[0]->ToObject();
    bool useMd5 = true;
    if (!args[1]->IsUndefined())
    {
      String::Utf8Value utfVal(args[1]->ToString());
      string hasher(*utfVal);
      useMd5 = hasher != "murmur";
    }
    HashRing *hash_ring = new HashRing(weight_hash, useMd5);
    hash_ring->Wrap(args.Holder());
    args.GetReturnValue().Set(args.Holder());
  }
  else
  {
    isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "HashRing Bad argument")));
  }
}

void HashRing::GetNode(const FunctionCallbackInfo<Value> &args)
{
  Isolate *isolate = args.GetIsolate();

  HashRing *hash_ring = ObjectWrap::Unwrap<HashRing>(args.Holder());
  Ring &ring = hash_ring->ring;

  Local<String> str = args[0]->ToString();
  String::Utf8Value utfVal(str);
  char *key = *utfVal;
  size_t h = hash_ring->hash_val(key, utfVal.length());

  int high = ring.num_points;
  const vector<Vpoint> &vpoints = ring.vpoints;
  int low = 0, mid;
  size_t mid_val, mid_val_1;
  while (true)
  {
    mid = (int)((low + high) / 2);
    if (mid == ring.num_points)
    {
      args.GetReturnValue().Set(String::NewFromUtf8(isolate, ring.getNodeId(vpoints[0]).c_str())); // We're at the end. Go to 0
      return;
    }
    mid_val = vpoints[mid].point;
    mid_val_1 = mid == 0 ? 0 : vpoints[mid - 1].point;
    if (h <= mid_val && h > mid_val_1)
    {
      args.GetReturnValue().Set(String::NewFromUtf8(isolate, ring.getNodeId(vpoints[mid]).c_str()));
      return;
    }
    if (mid_val < h)
    {
      low = mid + 1;
    }
    else
    {
      high = mid - 1;
    }

    if (low > high)
    {
      args.GetReturnValue().Set(String::NewFromUtf8(isolate, ring.getNodeId(vpoints[0]).c_str()));
      return;
    }
  }
}
}
