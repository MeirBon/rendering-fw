/* disney2.h - License information:

   This code has been adapted from AppleSeed: https://appleseedhq.net
   The AppleSeed software is released under the MIT license.
   Copyright (c) 2014-2018 Esteban Tovagliari, The appleseedhq Organization.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   // https://github.com/appleseedhq/appleseed/blob/master/src/appleseed/renderer/modeling/bsdf/disneybrdf.cpp
*/

#ifndef DISNEY2_H
#define DISNEY2_H

#include "compatibility.h"

#define DIFFWEIGHT	weights.x
#define SHEENWEIGHT	weights.y
#define SPECWEIGHT	weights.z
#define COATWEIGHT	weights.w

#define GGXMDF		1001
#define GTR1MDF		1002

struct InputValues
{
	float sheen, metallic, specular, clearcoat, clearcoat_gloss, roughness, anisotropic, subsurface, sheen_tint, specular_tint;
	float3 tint_color, base_color;
	float base_color_luminance;
};

LH2_DEVFUNC float schlick_fresnel( const float u )
{
	const float m = saturate( 1.0f - u ), m2 = sqr( m ), m4 = sqr( m2 );
	return m4 * m;
}
LH2_DEVFUNC void mix_spectra( const float3 a, const float3 b, const float t, REFERENCE_OF( float3 ) result ) { result = (1.0f - t) * a + t * b; }
LH2_DEVFUNC void mix_one_with_spectra( const float3 b, const float t, REFERENCE_OF( float3 ) result ) { result = (1.0f - t) + t * b; }
LH2_DEVFUNC void mix_spectra_with_one( const float3 a, const float t, REFERENCE_OF( float3 ) result ) { result = (1.0f - t) * a + t; }
LH2_DEVFUNC float microfacet_alpha_from_roughness( const float roughness ) { return max( 0.001f, roughness * roughness ); }
LH2_DEVFUNC void microfacet_alpha_from_roughness( const float roughness, const float anisotropy, REFERENCE_OF( float ) alpha_x, REFERENCE_OF( float ) alpha_y )
{
	const float square_roughness = roughness * roughness;
	const float aspect = sqrtf( 1.0f + anisotropy * (anisotropy < 0 ? 0.9f : -0.9f) );
	alpha_x = max( 0.001f, square_roughness / aspect );
	alpha_y = max( 0.001f, square_roughness * aspect );
}
LH2_DEVFUNC float clearcoat_roughness( const InputValues disney ) { return mix( 0.1f, 0.001f, disney.clearcoat_gloss ); }
LH2_DEVFUNC void DisneySpecularFresnel( const InputValues disney, const float3 o, const float3 h, REFERENCE_OF( float3 ) value )
{
	mix_one_with_spectra( disney.tint_color, disney.specular_tint, value );
	value *= disney.specular * 0.08f;
	mix_spectra( value, disney.base_color, disney.metallic, value );
	const float cos_oh = fabs( dot( o, h ) );
	mix_spectra_with_one( value, schlick_fresnel( cos_oh ), value );
}
LH2_DEVFUNC void DisneyClearcoatFresnel( const InputValues disney, const float3 o, const float3 h, REFERENCE_OF( float3 ) value )
{
	const float cos_oh = fabs( dot( o, h ) );
	value = make_float3( mix( 0.04f, 1.0f, schlick_fresnel( cos_oh ) ) * 0.25f * disney.clearcoat );
}
LH2_DEVFUNC bool force_above_surface( REFERENCE_OF( float3 ) direction, const float3 normal )
{
	const float Eps = 1.0e-4f;
	const float cos_theta = dot( direction, normal );
	const float correction = Eps - cos_theta;
	if (correction <= 0) return false;
	direction = normalize( direction + correction * normal );
	return true;
}
LH2_DEVFUNC float3 linear_rgb_to_ciexyz( const float3 rgb )
{
	return make_float3(
		max( 0.0f, 0.412453f * rgb.x + 0.357580f * rgb.y + 0.180423f * rgb.z ),
		max( 0.0f, 0.212671f * rgb.x + 0.715160f * rgb.y + 0.072169f * rgb.z ),
		max( 0.0f, 0.019334f * rgb.x + 0.119193f * rgb.y + 0.950227f * rgb.z ) );
}
LH2_DEVFUNC float3 ciexyz_to_linear_rgb( const float3 xyz )
{
	return make_float3(
		max( 0.0f, 3.240479f * xyz.x - 1.537150f * xyz.y - 0.498535f * xyz.z ),
		max( 0.0f, -0.969256f * xyz.x + 1.875991f * xyz.y + 0.041556f * xyz.z ),
		max( 0.0f, 0.055648f * xyz.x - 0.204043f * xyz.y + 1.057311f * xyz.z ) );
}

template <uint MDF, bool flip>
LH2_DEVFUNC void sample_mf( const InputValues disney, const float r0, const float r1, const float alpha_x, const float alpha_y, const float3 iN, const float3 wow,
	/* OUT: */ REFERENCE_OF( float3 ) wiw, REFERENCE_OF( float ) pdf, REFERENCE_OF( float3 ) value )
{
	float3 wo = World2Tangent( wow, iN ); // local_geometry.m_shading_basis.transform_to_local( outgoing );
	if (wo.z == 0) return;
	if (flip) wo.z = fabs( wo.z );
	// compute the incoming direction by sampling the MDF
	float3 m = MDF == GGXMDF ? GGXMDF_sample( wo, r0, r1, alpha_x, alpha_y ) : GTR1MDF_sample( r0, r1, alpha_x, alpha_y );
	float3 wi = reflect( wo * -1.0f, m );
	// force the outgoing direction to lie above the geometric surface.
	const float3 ng = make_float3( 0, 0, 1 ); // TODO: this should be the geometric normal moved to a tangent space setup using the interpolated normal. local_geometry.m_shading_basis.transform_to_local( local_geometry.m_geometric_normal );
	if (force_above_surface( wi, ng )) m = normalize( wo + wi );
	if (wi.z == 0) return;
	const float cos_oh = dot( wo, m );
	pdf = (MDF == GGXMDF ? GGXMDF_pdf( wo, m, alpha_x, alpha_y ) : GTR1MDF_pdf( wo, m, alpha_x, alpha_y )) / fabs( 4.0f * cos_oh );
	/* assert( pdf >= 0 ); */
	if (pdf < 1.0e-6f) return; // skip samples with very low probability
	const float D = MDF == GGXMDF ? GGXMDF_D( m, alpha_x, alpha_y ) : GTR1MDF_D( m, alpha_x, alpha_y );
	const float G = MDF == GGXMDF ? GGXMDF_G( wi, wo, m, alpha_x, alpha_y ) : GTR1MDF_G( wi, wo, m, alpha_x, alpha_y );
	if (MDF == GGXMDF) DisneySpecularFresnel( disney, wo, m, value ); else DisneyClearcoatFresnel( disney, wo, m, value );
	value *= D * G / fabs( 4.0f * wo.z * wi.z );
	wiw = Tangent2World( wi, iN );
}

template <uint MDF, bool flip>
LH2_DEVFUNC float evaluate_mf( const InputValues disney, const float alpha_x, const float alpha_y, const float3 iN, const float3 wow, const float3 wiw, REFERENCE_OF( float3 ) bsdf )
{
	float3 wo = World2Tangent( wow, iN );
	float3 wi = World2Tangent( wiw, iN );
	if (wo.z == 0 || wi.z == 0) return 0;
	// flip the incoming and outgoing vectors to be in the same hemisphere as the shading normal if needed.
	if (flip) wo.z = fabs( wo.z ), wi.z = fabs( wi.z );
	const float3 m = normalize( wi + wo );
	const float cos_oh = dot( wo, m );
	if (cos_oh == 0) return 0;
	const float D = MDF == GGXMDF ? GGXMDF_D( m, alpha_x, alpha_y ) : GTR1MDF_D( m, alpha_x, alpha_y );
	const float G = MDF == GGXMDF ? GGXMDF_G( wi, wo, m, alpha_x, alpha_y ) : GTR1MDF_G( wi, wo, m, alpha_x, alpha_y );
	if (MDF == GGXMDF) DisneySpecularFresnel( disney, wo, m, bsdf ); else DisneyClearcoatFresnel( disney, wo, m, bsdf );
	bsdf *= D * G / fabs( 4.0f * wo.z * wi.z );
	return (MDF == GGXMDF ? GGXMDF_pdf( wo, m, alpha_x, alpha_y ) : GTR1MDF_pdf( wo, m, alpha_x, alpha_y )) / fabs( 4.0f * cos_oh );
}

template <uint MDF, bool flip>
LH2_DEVFUNC float pdf_mf( const float alpha_x, const float alpha_y, const float3 iN, const float3 wow, const float3 wiw )
{
	float3 wo = World2Tangent( wow, iN ); // local_geometry.m_shading_basis.transform_to_local( outgoing );
	float3 wi = World2Tangent( wiw, iN ); // local_geometry.m_shading_basis.transform_to_local( incoming );
	// flip the incoming and outgoing vectors to be in the same hemisphere as the shading normal if needed.
	if (flip) wo.z = fabs( wo.z ), wi.z = fabs( wi.z );
	const float3 m = normalize( wi + wo );
	const float cos_oh = dot( wo, m );
	if (cos_oh == 0) return 0;
	return (MDF == GGXMDF ? GGXMDF_pdf( wo, m, alpha_x, alpha_y ) : GTR1MDF_pdf( wo, m, alpha_x, alpha_y )) / fabs( 4.0f * cos_oh );
}

LH2_DEVFUNC float evaluate_diffuse( const InputValues disney, const float3 iN, const float3 wow, const float3 wiw, REFERENCE_OF( float3 ) value )
{
	// this code is mostly ported from the GLSL implementation in Disney's BRDF explorer.
	// const float3 n( local_geometry.m_shading_basis.get_normal() );
	const float3 n = iN;
	const float3 h( normalize( wiw + wow ) );
	// using the absolute values of cos_on and cos_in creates discontinuities
	const float cos_on = dot( n, wow );
	const float cos_in = dot( n, wiw );
	const float cos_ih = dot( wiw
	, h );
	const float fl = schlick_fresnel( cos_in );
	const float fv = schlick_fresnel( cos_on );
	float fd = 0;
	if (disney.subsurface != 1.0f)
	{
		const float fd90 = 0.5f + 2.0f * sqr( cos_ih ) * disney.roughness;
		fd = mix( 1.0f, fd90, fl ) * mix( 1.0f, fd90, fv );
	}
	if (disney.subsurface > 0)
	{
		// based on Hanrahan-Krueger BRDF approximation of isotropic BSRDF.
		// the 1.25 scale is used to (roughly) preserve albedo.
		// Fss90 is used to "flatten" retroreflection based on roughness.
		const float fss90 = sqr( cos_ih ) * disney.roughness;
		const float fss = mix( 1.0f, fss90, fl ) * mix( 1.0f, fss90, fv );
		const float ss = 1.25f * (fss * (1.0f / (fabs( cos_on ) + fabs( cos_in )) - 0.5f) + 0.5f);
		fd = mix( fd, ss, disney.subsurface );
	}
	value = disney.base_color * fd * INVPI * (1.0f - disney.metallic);
	return fabs( cos_in ) * INVPI;
}

LH2_DEVFUNC void sample_diffuse( const InputValues disney, const float r0, const float r1, const float3 iN, const float3 wow,
	/* OUT: */ REFERENCE_OF( float3 ) wiw, REFERENCE_OF( float ) pdf, REFERENCE_OF( float3 ) value )
{
	// compute the incoming direction
	const float3 wi = DiffuseReflectionCosWeighted( r0, r1 );
	wiw = normalize( Tangent2World( wi, iN ) );
	// compute the component value and the probability density of the sampled direction.
	pdf = evaluate_diffuse( disney, iN, wow, wiw, value );
	/* assert( pdf > 0 ); */
	if (pdf < 1.0e-6f) return;
}

LH2_DEVFUNC float evaluate_sheen( const InputValues disney, const float3 wow, const float3 wiw, REFERENCE_OF( float3 ) value )
{
	// this code is mostly ported from the GLSL implementation in Disney's BRDF explorer.
	const float3 h( normalize( wow + wow ) );
	const float cos_ih = dot( wiw, h );
	const float fh = schlick_fresnel( cos_ih );
	mix_one_with_spectra( disney.tint_color, disney.sheen_tint, value );
	value *= fh * disney.sheen * (1.0f - disney.metallic);
	return 1.0f / (2 * PI); // return the probability density of the sampled direction
}

LH2_DEVFUNC void sample_sheen( const InputValues disney, const float r0, const float r1, const float3 iN, const float3 wow,
	/* OUT: */ REFERENCE_OF( float3 ) wiw, REFERENCE_OF( float ) pdf, REFERENCE_OF( float3 ) value )
{
	// compute the incoming direction
	const float3 wi = DiffuseReflectionCosWeighted( r0, r1 );
	wiw = normalize( Tangent2World( wi, iN ) );
	// compute the component value and the probability density of the sampled direction
	pdf = evaluate_sheen( disney, wow, wiw, value );
	/* assert( pdf > 0 ); */
	if (pdf < 1.0e-6f) return;
}

LH2_DEVFUNC void sample_disney( const InputValues disney, const float r0, const float r1, const float3 iN, const float3 wow,
	/* OUT: */ REFERENCE_OF( float3 ) wiw, REFERENCE_OF( float ) pdf, REFERENCE_OF( float3 ) value )
{
	// compute component weights and cdf
	float4 weights = make_float4( 
		lerp( disney.base_color_luminance, 0, disney.metallic ), 
		lerp( disney.sheen, 0, disney.metallic ), 
		lerp( disney.specular, 1, disney.metallic ), 
		disney.clearcoat * 0.25f 
	);
	weights *= 1.0f / (weights.x + weights.y + weights.z + weights.w);
	const float4 cdf = make_float4( weights.x, weights.x + weights.y, weights.x + weights.y + weights.z, 0 );
	// sample a random component
	float probability, component_pdf;
	float3 contrib;
	if (r0 < cdf.x)
	{
		const float r2 = r0 / cdf.x; // reuse r0 after normalization
		sample_diffuse( disney, r2, r1, iN, wow, wiw, component_pdf, value );
		probability = DIFFWEIGHT * component_pdf, DIFFWEIGHT = 0;
	}
	else if (r0 < cdf.y)
	{
		const float r2 = (r0 - cdf.x) / (cdf.y - cdf.x); // reuse r0 after normalization
		sample_sheen( disney, r2, r1, iN, wow, wiw, component_pdf, value );
		probability = SHEENWEIGHT * component_pdf, SHEENWEIGHT = 0;
	}
	else if (r0 < cdf.z)
	{
		const float r2 = (r0 - cdf.y) / (cdf.z - cdf.y); // reuse r0 after normalization
		float alpha_x, alpha_y;
		microfacet_alpha_from_roughness( disney.roughness, disney.anisotropic, alpha_x, alpha_y );
		sample_mf<GGXMDF, false>( disney, r2, r1, alpha_x, alpha_y, iN, wow, wiw, component_pdf, value );
		probability = SPECWEIGHT * component_pdf, SPECWEIGHT = 0;
	}
	else
	{
		const float r2 = (r0 - cdf.z) / (1 - cdf.z); // reuse r0 after normalization
		const float alpha = clearcoat_roughness( disney );
		sample_mf<GTR1MDF, false>( disney, r2, r1, alpha, alpha, iN, wow, wiw, component_pdf, value );
		probability = COATWEIGHT * component_pdf, COATWEIGHT = 0;
	}
	if (DIFFWEIGHT > 0) probability += DIFFWEIGHT * evaluate_diffuse( disney, iN, wow, wiw, contrib ), value += contrib;
	if (SHEENWEIGHT > 0) probability += SHEENWEIGHT * evaluate_sheen( disney, wow, wiw, contrib ), value += contrib;
	if (SPECWEIGHT > 0)
	{
		float alpha_x, alpha_y;
		microfacet_alpha_from_roughness( disney.roughness, disney.anisotropic, alpha_x, alpha_y );
		probability += SPECWEIGHT * evaluate_mf<GGXMDF, false>( disney, alpha_x, alpha_y, iN, wow, wiw, contrib );
		value += contrib;
	}
	if (COATWEIGHT > 0)
	{
		const float alpha = clearcoat_roughness( disney );
		probability += COATWEIGHT * evaluate_mf<GTR1MDF, false>( disney, alpha, alpha, iN, wow, wiw, contrib );
		value += contrib;
	}
	if (probability > 1.0e-6f) pdf = probability; else pdf = 0;
}

LH2_DEVFUNC float evaluate_disney( const InputValues disney, const float3 iN, const float3 wow, const float3 wiw, REFERENCE_OF( float3 ) value )
{
	// compute component weights
	float4 weights = make_float4( lerp( disney.base_color_luminance, 0, disney.metallic ), lerp( disney.sheen, 0, disney.metallic ), lerp( disney.specular, 1, disney.metallic ), disney.clearcoat * 0.25f );
	weights *= 1.0f / (weights.x + weights.y + weights.z + weights.w);
	// compute pdf
	float pdf = 0;
	value = make_float3( 0 );
	if (DIFFWEIGHT > 0) pdf += DIFFWEIGHT * evaluate_diffuse( disney, iN, wow, wiw, value );
	if (SHEENWEIGHT > 0) pdf += SHEENWEIGHT * evaluate_sheen( disney, wow, wiw, value );
	if (SPECWEIGHT > 0)
	{
		float alpha_x, alpha_y;
		microfacet_alpha_from_roughness( disney.roughness, disney.anisotropic, alpha_x, alpha_y );
		float3 contrib;
		const float spec_pdf = evaluate_mf<GGXMDF, false>( disney, alpha_x, alpha_y, iN, wow, wiw, contrib );
		if (spec_pdf > 0) pdf += SPECWEIGHT * spec_pdf, value += contrib;
	}
	if (COATWEIGHT > 0)
	{
		const float alpha = clearcoat_roughness( disney );
		float3 contrib;
		const float clearcoat_pdf = evaluate_mf<GTR1MDF, false>( disney, alpha, alpha, iN, wow, wiw, contrib );
		if (clearcoat_pdf > 0) pdf += COATWEIGHT * clearcoat_pdf, value += contrib;
	}
	/* assert( pdf >= 0 ); */ return pdf;
}

LH2_DEVFUNC float evaluate_pdf( const InputValues disney, const float3 iN, const float3 wow, const float3 wiw )
{
	// compute component weights
	float4 weights = make_float4( lerp( disney.base_color_luminance, 0, disney.metallic ), lerp( disney.sheen, 0, disney.metallic ), lerp( disney.specular, 1, disney.metallic ), disney.clearcoat * 0.25f );
	weights *= 1.0f / (weights.x + weights.y + weights.z + weights.w);
	// compute pdf
	float pdf = 0;
	if (DIFFWEIGHT > 0) pdf += DIFFWEIGHT * fabs( dot( wiw, iN ) ) * INVPI;
	if (SHEENWEIGHT > 0) pdf += SHEENWEIGHT * (1.0f / (2 * PI));
	if (SPECWEIGHT > 0)
	{
		float alpha_x, alpha_y;
		microfacet_alpha_from_roughness( disney.roughness, disney.anisotropic, alpha_x, alpha_y );
		pdf += SPECWEIGHT * pdf_mf<GGXMDF, false>( alpha_x, alpha_y, iN, wow, wiw );
	}
	if (COATWEIGHT > 0)
	{
		const float alpha = clearcoat_roughness( disney );
		pdf += COATWEIGHT * pdf_mf<GTR1MDF, false>( alpha, alpha, iN, wow, wiw );
	}
	/* assert( pdf >= 0 ); */ return pdf;
}

// ============================================
// CONVERSION
// ============================================

LH2_DEVFUNC float3 EvaluateBSDF( const ShadingData shadingData, const float3 iN, const float3 T,
	const float3 wo, const float3 wi, REFERENCE_OF( float ) pdf )
{
	InputValues disney;
	disney.sheen = SHEEN;
	disney.metallic = METALLIC;
	disney.specular = SPECULAR;
	disney.clearcoat = CLEARCOAT;
	disney.clearcoat_gloss = CLEARCOATGLOSS;
	disney.roughness = ROUGHNESS;
	disney.anisotropic = ANISOTROPIC;
	disney.subsurface = SUBSURFACE;
	disney.sheen_tint = SHEENTINT;
	disney.specular_tint = SPECTINT;
	disney.base_color = shadingData.color;
	const float3 tint_xyz = linear_rgb_to_ciexyz( disney.base_color );
	disney.tint_color = tint_xyz.y > 0 ? ciexyz_to_linear_rgb( tint_xyz * (1.0f / tint_xyz.y) ) : make_float3( 1 );
	disney.base_color_luminance = tint_xyz.y;
	float3 value;
	pdf = evaluate_disney( disney, iN, wo, wi, value );
	return value;
}

LH2_DEVFUNC float3 SampleBSDF( const ShadingData shadingData, float3 iN, const float3 N, const float3 T, const float3 wo, const float distance,
	const float r3, const float r4, REFERENCE_OF( float3 ) wi, REFERENCE_OF( float ) pdf, REFERENCE_OF( bool ) specular
#ifdef __CUDACC__
	, bool adjoint = false
#endif
)
{
	InputValues disney;
	disney.sheen = SHEEN;
	disney.metallic = METALLIC;
	disney.specular = SPECULAR;
	disney.clearcoat = CLEARCOAT;
	disney.clearcoat_gloss = CLEARCOATGLOSS;
	disney.roughness = ROUGHNESS;
	disney.anisotropic = ANISOTROPIC;
	disney.subsurface = SUBSURFACE;
	disney.sheen_tint = SHEENTINT;
	disney.specular_tint = SPECTINT;
	disney.base_color = shadingData.color;
	const float3 tint_xyz = linear_rgb_to_ciexyz( disney.base_color );
	disney.tint_color = tint_xyz.y > 0 ? ciexyz_to_linear_rgb( tint_xyz * (1.0f / tint_xyz.y) ) : make_float3( 1 );
	disney.base_color_luminance = tint_xyz.y;
	float3 value;
	sample_disney( disney, r3, r4, iN, wo, wi, pdf, value );
	return value;
}

#endif