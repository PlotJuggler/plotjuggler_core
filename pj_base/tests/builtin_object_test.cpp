#include <gtest/gtest.h>

#include "pj_base/builtin/BuiltinObject.h"

using PJ::sdk::BuiltinObject;
using PJ::sdk::BuiltinObjectType;
using PJ::sdk::DepthImage;
using PJ::sdk::FrameTransforms;
using PJ::sdk::Image;
using PJ::sdk::ImageAnnotations;
using PJ::sdk::parseBuiltinObjectType;
using PJ::sdk::PointCloud;
using PJ::sdk::typeOf;

TEST(BuiltinObjectTest, TypeOfRecognizesKnownBuiltinTypes) {
  EXPECT_EQ(typeOf(BuiltinObject{}), BuiltinObjectType::kNone);
  EXPECT_EQ(typeOf(BuiltinObject{Image{}}), BuiltinObjectType::kImage);
  EXPECT_EQ(typeOf(BuiltinObject{PointCloud{}}), BuiltinObjectType::kPointCloud);
  EXPECT_EQ(typeOf(BuiltinObject{DepthImage{}}), BuiltinObjectType::kDepthImage);
  EXPECT_EQ(typeOf(BuiltinObject{ImageAnnotations{}}), BuiltinObjectType::kImageAnnotations);
  EXPECT_EQ(typeOf(BuiltinObject{FrameTransforms{}}), BuiltinObjectType::kFrameTransforms);
}

TEST(BuiltinObjectTest, ParsesFrameTransformsTypeName) {
  EXPECT_EQ(parseBuiltinObjectType("kFrameTransforms"), BuiltinObjectType::kFrameTransforms);
  EXPECT_FALSE(parseBuiltinObjectType("FrameTransforms").has_value());
}
