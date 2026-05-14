#include "pj_plugins/sdk/object_ingest_policy.hpp"

#include <gtest/gtest.h>

using PJ::sdk::BuiltinObjectKind;
using PJ::sdk::ObjectIngestPolicy;
using PJ::sdk::ObjectIngestPolicyResolver;

TEST(ObjectIngestPolicyResolverTest, DefaultPolicyIsLazyObjectsEagerScalars) {
  ObjectIngestPolicyResolver r;
  EXPECT_EQ(
      r.resolve("any_source", "/any/topic", BuiltinObjectKind::kImage), ObjectIngestPolicy::kLazyObjectsEagerScalars);
}

TEST(ObjectIngestPolicyResolverTest, SetDefaultIsRespected) {
  ObjectIngestPolicyResolver r;
  r.setDefault(ObjectIngestPolicy::kEager);
  EXPECT_EQ(r.resolve("any_source", "/any/topic", BuiltinObjectKind::kImage), ObjectIngestPolicy::kEager);
}

TEST(ObjectIngestPolicyResolverTest, KindOverrideFiresOnMatch) {
  ObjectIngestPolicyResolver r;
  r.setDefault(ObjectIngestPolicy::kLazyObjectsEagerScalars);
  r.setForKind(BuiltinObjectKind::kPointCloud, ObjectIngestPolicy::kPureLazy);

  EXPECT_EQ(r.resolve("src", "/lidar/points", BuiltinObjectKind::kPointCloud), ObjectIngestPolicy::kPureLazy);
  // Different kind falls through to default.
  EXPECT_EQ(r.resolve("src", "/cam/image", BuiltinObjectKind::kImage), ObjectIngestPolicy::kLazyObjectsEagerScalars);
}

TEST(ObjectIngestPolicyResolverTest, SourceOverridesKind) {
  ObjectIngestPolicyResolver r;
  r.setDefault(ObjectIngestPolicy::kLazyObjectsEagerScalars);
  r.setForKind(BuiltinObjectKind::kPointCloud, ObjectIngestPolicy::kPureLazy);
  r.setForDataSource("mcap_source", ObjectIngestPolicy::kEager);

  // Source matches → kEager beats the kPointCloud kind override.
  EXPECT_EQ(r.resolve("mcap_source", "/lidar/points", BuiltinObjectKind::kPointCloud), ObjectIngestPolicy::kEager);
  // Different source → kind override fires.
  EXPECT_EQ(r.resolve("ros2_stream", "/lidar/points", BuiltinObjectKind::kPointCloud), ObjectIngestPolicy::kPureLazy);
}

TEST(ObjectIngestPolicyResolverTest, TopicOverridesEverything) {
  ObjectIngestPolicyResolver r;
  r.setDefault(ObjectIngestPolicy::kLazyObjectsEagerScalars);
  r.setForKind(BuiltinObjectKind::kPointCloud, ObjectIngestPolicy::kPureLazy);
  r.setForDataSource("mcap_source", ObjectIngestPolicy::kEager);
  r.setForTopic("/diagnostics/lidar", ObjectIngestPolicy::kPureLazy);

  // Topic match wins over source and kind.
  EXPECT_EQ(
      r.resolve("mcap_source", "/diagnostics/lidar", BuiltinObjectKind::kPointCloud), ObjectIngestPolicy::kPureLazy);
  // Different topic → source override fires.
  EXPECT_EQ(r.resolve("mcap_source", "/other/lidar", BuiltinObjectKind::kPointCloud), ObjectIngestPolicy::kEager);
}

TEST(ObjectIngestPolicyResolverTest, TypicalApplicationSetup) {
  // Mirror the recommended setup: large blobs lazy by default, images keep
  // their metadata as columns. PointCloud is always pure-lazy; specific
  // compressed-image topics can be demoted via per-topic overrides when
  // their scalars aren't worth materializing.
  ObjectIngestPolicyResolver r;
  r.setDefault(ObjectIngestPolicy::kLazyObjectsEagerScalars);
  r.setForKind(BuiltinObjectKind::kPointCloud, ObjectIngestPolicy::kPureLazy);
  r.setForTopic("/cam/jpeg", ObjectIngestPolicy::kPureLazy);

  EXPECT_EQ(r.resolve("mcap", "/cam/raw", BuiltinObjectKind::kImage), ObjectIngestPolicy::kLazyObjectsEagerScalars);
  EXPECT_EQ(r.resolve("mcap", "/cam/jpeg", BuiltinObjectKind::kImage), ObjectIngestPolicy::kPureLazy);
  EXPECT_EQ(r.resolve("mcap", "/lidar", BuiltinObjectKind::kPointCloud), ObjectIngestPolicy::kPureLazy);
  // Scalar-only topic (no builtin object) takes the default.
  EXPECT_EQ(r.resolve("mcap", "/diagnostics", BuiltinObjectKind::kNone), ObjectIngestPolicy::kLazyObjectsEagerScalars);
}

TEST(ObjectIngestPolicyResolverTest, LastWriteWinsForSameKey) {
  ObjectIngestPolicyResolver r;
  r.setForKind(BuiltinObjectKind::kImage, ObjectIngestPolicy::kEager);
  r.setForKind(BuiltinObjectKind::kImage, ObjectIngestPolicy::kPureLazy);
  EXPECT_EQ(r.resolve("src", "/topic", BuiltinObjectKind::kImage), ObjectIngestPolicy::kPureLazy);

  r.setForTopic("/x", ObjectIngestPolicy::kLazyObjectsEagerScalars);
  r.setForTopic("/x", ObjectIngestPolicy::kEager);
  EXPECT_EQ(r.resolve("src", "/x", BuiltinObjectKind::kImage), ObjectIngestPolicy::kEager);
}
