//
// Copyright (c) 2008-2016 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../IK/IK.h"
#include "../IK/IKConstraint.h"
#include "../Core/Context.h"
#include "../Scene/Node.h"
#include "../Scene/SceneEvents.h"
#include <ik/node.h>

namespace Urho3D
{

extern const char* IK_CATEGORY;

// ----------------------------------------------------------------------------
IKConstraint::IKConstraint(Context* context) :
    Component(context),
    stiffness_(0.0f),
    stretchiness_(0.0f)
{
}

// ----------------------------------------------------------------------------
IKConstraint::~IKConstraint()
{
}

// ----------------------------------------------------------------------------
void IKConstraint::RegisterObject(Context* context)
{
    context->RegisterFactory<IKConstraint>(IK_CATEGORY);

    URHO3D_ACCESSOR_ATTRIBUTE("Stiffness", GetStiffness, SetStiffness, float, 0.0f, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Stretchiness", GetStretchiness, SetStretchiness, float, 0.0f, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Length Constraints", GetLengthConstraints, SetLengthConstraints, Vector2, Vector2::ZERO, AM_DEFAULT);
}

// ----------------------------------------------------------------------------
float IKConstraint::GetStiffness() const
{
    return stiffness_;
}

// ----------------------------------------------------------------------------
void IKConstraint::SetStiffness(float stiffness)
{
    stiffness_ = Clamp(stiffness, 0.0f, 1.0f);
    if (ikNode_ != NULL)
        /* TODO ikNode_->stiffness = stiffness_; */
        ;
}

// ----------------------------------------------------------------------------
float IKConstraint::GetStretchiness() const
{
    return stretchiness_;
}

// ----------------------------------------------------------------------------
void IKConstraint::SetStretchiness(float stretchiness)
{
    stretchiness_ = Clamp(stretchiness, 0.0f, 1.0f);
    if (ikNode_)
        /* TODO ikNode_->stretchiness = stretchiness_;*/
        ;
}

// ----------------------------------------------------------------------------
const Vector2& IKConstraint::GetLengthConstraints() const
{
    return lengthConstraints_;
}

// ----------------------------------------------------------------------------
void IKConstraint::SetLengthConstraints(const Vector2& lengthConstraints)
{
    lengthConstraints_ = lengthConstraints;
    if (ikNode_ != NULL)
    {
        /* TODO
        ikNode_->min_length = lengthConstraints_.x_;
        ikNode_->max_length = lengthConstraints_.y_;*/
    }
}

// ----------------------------------------------------------------------------
void IKConstraint::SetIKNode(ik_node_t* node)
{
    ikNode_ = node;
    if (node)
    {
        /* TODO
        node->stiffness = stiffness_;
        node->stretchiness = stretchiness_;
        node->min_length = lengthConstraints_.x_;
        node->max_length = lengthConstraints_.y_;*/
    }
}

} // namespace Urho3D
