pub type Vec3 = [f32; 3];

const SQRT_SEED_BIAS: u32 = 0x1fbd_1df5;
const SQRT_NEWTON_STEPS: usize = 3;

pub fn abs(x: f32) -> f32 {
    if x < 0.0 {
        -x
    } else {
        x
    }
}

pub fn sqrt(x: f32) -> f32 {
    if x <= 0.0 {
        return 0.0;
    }
    let mut guess = f32::from_bits((x.to_bits() >> 1) + SQRT_SEED_BIAS);
    for _ in 0..SQRT_NEWTON_STEPS {
        guess = 0.5 * (guess + x / guess);
    }
    guess
}

pub fn dot(a: Vec3, b: Vec3) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

pub fn norm(a: Vec3) -> f32 {
    sqrt(dot(a, a))
}

pub fn scale(a: Vec3, k: f32) -> Vec3 {
    [a[0] * k, a[1] * k, a[2] * k]
}

pub fn sub(a: Vec3, b: Vec3) -> Vec3 {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}

pub fn cross(a: Vec3, b: Vec3) -> Vec3 {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

pub fn hypot(x: f32, y: f32) -> f32 {
    sqrt(x * x + y * y)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sqrt_matches_std_across_the_g_range() {
        for i in 1..400 {
            let x = i as f32 * 0.01;
            assert!(abs(sqrt(x) - x.sqrt()) < 1e-5, "sqrt({x})");
        }
    }

    #[test]
    fn sqrt_of_zero_or_negative_is_zero() {
        assert_eq!(sqrt(0.0), 0.0);
        assert_eq!(sqrt(-1.0), 0.0);
    }

    #[test]
    fn cross_follows_the_right_hand_rule() {
        assert_eq!(cross([1.0, 0.0, 0.0], [0.0, 1.0, 0.0]), [0.0, 0.0, 1.0]);
        assert_eq!(cross([0.0, 0.0, -1.0], [0.0, 1.0, 0.0]), [1.0, 0.0, 0.0]);
    }
}
