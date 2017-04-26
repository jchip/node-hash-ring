#ifndef HASH_RING_H_
#define HASH_RING_H_

#include <node.h>
#include <node_object_wrap.h>
#include <string>
#include <vector>

namespace HashRing
{
typedef int (*compfn)(const void *, const void *);

class NodeInfo
{
public:
  std::string id;
  unsigned int weight;

  void set(const char *idStr, unsigned int w)
  {
    id = idStr;
    weight = w;
  }

  NodeInfo() : weight(0)
  {
  }

  ~NodeInfo()
  {
  }
};

class Vpoint
{
public:
  unsigned int point;
  size_t node_index;

  void set(size_t idx, unsigned int pt)
  {
    node_index = idx;
    point = pt;
  }

  Vpoint() : point(0), node_index(0)
  {
  }
  ~Vpoint() {}
};

class Ring
{
public:
  int num_points;
  int num_servers;
  std::vector<Vpoint> vpoints;
  std::vector<NodeInfo> node_list;
  Ring() : num_points(0), num_servers(0)
  {
  }

  void setNumServers(int ns)
  {
    num_servers = ns;
    node_list.resize(ns);
  }

  void setNumPoints(int np)
  {
    num_points = np;
    vpoints.resize(np);
  }

  const std::string &getNodeId(const Vpoint &vpoint)
  {
    return node_list[vpoint.node_index].id;
  }

  ~Ring()
  {
  }
};

class HashRing : public node::ObjectWrap
{

  Ring ring;

public:
  HashRing(v8::Local<v8::Object> weight_hash, bool);
  ~HashRing();

  static void Initialize(v8::Local<v8::Object> exports);

private:
  bool useMd5;
  static void New(const v8::FunctionCallbackInfo<v8::Value> &args);
  static void GetNode(const v8::FunctionCallbackInfo<v8::Value> &args);
  //
  void hash_digest(char *in, int len, unsigned char out[16]);
  unsigned int hash_val(char *in, int len);
  static int vpoint_compare(Vpoint *a, Vpoint *b);
  static v8::Persistent<v8::Function> constructor;
};
}
#endif // HASH_RING_H_
