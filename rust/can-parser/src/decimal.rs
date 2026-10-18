use core::cmp::Ordering;

pub(crate) const MAX_DIGITS: usize = 128;

const LIMBS: usize = 16;
const LIMB_BITS: u32 = u32::BITS;
const QUOTIENT_TOP_BIT: u32 = 25;
const ROUNDED_QUOTIENT: u32 = 1 << QUOTIENT_TOP_BIT;
const SUBNORMAL_SCALE: i32 = 150;
const EXPONENT_OFFSET: i32 = 151;
const MANTISSA_FIELD_BITS: u32 = 23;
const IMPLICIT_BIT: i64 = 1 << MANTISSA_FIELD_BITS;
const INFINITY_BITS: i64 = 0x7F80_0000;

#[derive(Clone, Copy, PartialEq, Eq)]
struct Big([u32; LIMBS]);

impl Big {
    const fn from_small(value: u32) -> Self {
        let mut limbs = [0; LIMBS];
        limbs[0] = value;
        Big(limbs)
    }

    fn mul_add(&mut self, mul: u32, add: u32) {
        let mut carry = u64::from(add);
        for limb in self.0.iter_mut() {
            let wide = u64::from(*limb) * u64::from(mul) + carry;
            *limb = wide as u32;
            carry = wide >> LIMB_BITS;
        }
    }

    fn shl(&mut self, bits: u32) {
        let words = (bits / LIMB_BITS) as usize;
        let rest = bits % LIMB_BITS;
        for i in (0..LIMBS).rev() {
            let high = self.limb_below(i, words);
            let low = self.limb_below(i, words + 1);
            self.0[i] = if rest == 0 {
                high
            } else {
                (high << rest) | (low >> (LIMB_BITS - rest))
            };
        }
    }

    fn limb_below(&self, i: usize, offset: usize) -> u32 {
        i.checked_sub(offset).map_or(0, |j| self.0[j])
    }

    fn sub(&mut self, other: &Big) {
        let mut borrow = false;
        for (limb, &theirs) in self.0.iter_mut().zip(other.0.iter()) {
            let (diff, under) = limb.overflowing_sub(theirs);
            let (diff, under_borrow) = diff.overflowing_sub(u32::from(borrow));
            *limb = diff;
            borrow = under || under_borrow;
        }
    }

    fn bit_len(&self) -> i32 {
        self.0.iter().rposition(|&limb| limb != 0).map_or(0, |top| {
            (top as u32 * LIMB_BITS + LIMB_BITS - self.0[top].leading_zeros()) as i32
        })
    }

    fn is_zero(&self) -> bool {
        self.0.iter().all(|&limb| limb == 0)
    }
}

impl Ord for Big {
    fn cmp(&self, other: &Self) -> Ordering {
        self.0.iter().rev().cmp(other.0.iter().rev())
    }
}

impl PartialOrd for Big {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

fn read_digits(text: &[u8]) -> Option<(Big, usize)> {
    if text.len() > MAX_DIGITS + 1 {
        return None;
    }
    let mut value = Big::from_small(0);
    let mut fraction_digits = 0usize;
    let mut seen_dot = false;
    let mut seen_digit = false;
    for &c in text {
        if c == b'.' && !seen_dot {
            seen_dot = true;
            continue;
        }
        if !c.is_ascii_digit() {
            return None;
        }
        value.mul_add(10, u32::from(c - b'0'));
        seen_digit = true;
        fraction_digits += usize::from(seen_dot);
    }
    seen_digit.then_some((value, fraction_digits))
}

fn divide(mut numerator: Big, denominator: &Big) -> (u32, bool) {
    let mut quotient = 0u32;
    for bit in (0..=QUOTIENT_TOP_BIT).rev() {
        let mut shifted = *denominator;
        shifted.shl(bit);
        if numerator >= shifted {
            numerator.sub(&shifted);
            quotient |= 1 << bit;
        }
    }
    (quotient, !numerator.is_zero())
}

fn encode(quotient: u32, scale: i32, sticky: bool) -> u32 {
    let (quotient, scale, sticky) = if quotient >= ROUNDED_QUOTIENT {
        (quotient >> 1, scale - 1, sticky || quotient & 1 == 1)
    } else {
        (quotient, scale, sticky)
    };
    let half = quotient & 1 == 1;
    let mut mantissa = i64::from(quotient >> 1);
    if half && (sticky || mantissa & 1 == 1) {
        mantissa += 1;
    }
    let bits =
        (i64::from(EXPONENT_OFFSET - scale) << MANTISSA_FIELD_BITS) + mantissa - IMPLICIT_BIT;
    bits.min(INFINITY_BITS) as u32
}

fn round_quotient(mut numerator: Big, mut denominator: Big) -> u32 {
    let natural = QUOTIENT_TOP_BIT as i32 + denominator.bit_len() - numerator.bit_len();
    let scale = natural.min(SUBNORMAL_SCALE);
    if scale >= 0 {
        numerator.shl(scale as u32);
    } else {
        denominator.shl(scale.unsigned_abs());
    }
    let (quotient, sticky) = divide(numerator, &denominator);
    encode(quotient, scale, sticky)
}

pub(crate) fn parse_decimal(text: &[u8]) -> Option<f32> {
    let (numerator, fraction_digits) = read_digits(text)?;
    if numerator.is_zero() {
        return Some(0.0);
    }
    let mut denominator = Big::from_small(1);
    for _ in 0..fraction_digits {
        denominator.mul_add(10, 0);
    }
    Some(f32::from_bits(round_quotient(numerator, denominator)))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn assert_same(text: &str) {
        let expected = text.parse::<f32>().ok().map(f32::to_bits);
        let actual = parse_decimal(text.as_bytes()).map(f32::to_bits);
        assert_eq!(actual, expected, "{text}");
    }

    struct Lcg(u64);

    impl Lcg {
        fn next(&mut self) -> u64 {
            self.0 = self
                .0
                .wrapping_mul(6_364_136_223_846_793_005)
                .wrapping_add(1_442_695_040_888_963_407);
            self.0 >> 33
        }

        fn below(&mut self, bound: u64) -> u64 {
            self.next() % bound
        }
    }

    fn random_literal(rng: &mut Lcg) -> String {
        let len = 1 + rng.below(40) as usize;
        let mut text: String = (0..len)
            .map(|_| char::from(b'0' + rng.below(10) as u8))
            .collect();
        let dot = rng.below(len as u64 + 2) as usize;
        if dot <= len {
            text.insert(dot, '.');
        }
        text
    }

    fn exact_decimal(value: f64) -> String {
        let text = format!("{value:.1100}");
        let trimmed = text.trim_end_matches('0');
        trimmed.to_string()
    }

    const HAND_PICKED_LITERALS: &[&str] = &[
        "0",
        "0.0",
        "00",
        "1",
        "1.",
        ".5",
        "0.5",
        "0.1",
        "0.01",
        "0.001",
        "0.0625",
        "1.8",
        "32",
        "100",
        "273.15",
        "0.000015",
        "3.14159265358979",
        "16777216",
        "16777217",
        "16777218",
        "16777219",
        "33554431",
        "33554433",
        "4294967295",
        "4294967296",
        "0.1000000000000000055511151231257827",
        "340282346638528859811704183484516925440",
        "340282356779733661637539395458142568447",
        "340282356779733661637539395458142568448",
        "340282356779733661637539395458142568449",
        "999999999999999999999999999999999999999999",
        "0.000000000000000000000000000000000000011754943508222875",
        "0.000000000000000000000000000000000000000000001401298464324817",
        "0.0000000000000000000000000000000000000000000007006492321624085",
        "0.0000000000000000000000000000000000000000000007006492321624086",
        "0.0000000000000000000000000000000000000000000007",
        "0.00000000000000000000000000000000000000000000000001",
        "123456789.123456789",
        "000000000000000000000000000000000000001.5000000000000000000000000000000000",
        ".",
        "..",
        "1..2",
        "1.2.3",
        "..5",
        "5..",
    ];

    #[test]
    fn hand_picked_literals_match_core_parse() {
        for text in HAND_PICKED_LITERALS {
            assert_same(text);
        }
    }

    #[test]
    fn longest_literal_the_lexer_can_see_matches_core_parse() {
        assert_same(&"9".repeat(MAX_DIGITS));
        assert_same(&format!("0.{}", "0".repeat(MAX_DIGITS - 2) + "1"));
        assert_same(&format!("{}.{}", "1".repeat(64), "7".repeat(64)));
        assert_eq!(parse_decimal("1".repeat(MAX_DIGITS + 2).as_bytes()), None);
    }

    #[test]
    fn random_literals_match_core_parse() {
        let mut rng = Lcg(0x5EED);
        for _ in 0..60_000 {
            assert_same(&random_literal(&mut rng));
        }
    }

    #[test]
    fn halfway_points_between_floats_round_like_core_parse() {
        let mut rng = Lcg(0xF10A7);
        for _ in 0..10_000 {
            let bits = 0x3000_0000 + rng.below(0x1E00_0000) as u32;
            let low = f64::from(f32::from_bits(bits));
            let high = f64::from(f32::from_bits(bits + 1));
            let middle = exact_decimal((low + high) / 2.0);
            assert_same(&middle);
            assert_same(&format!("{middle}1"));
            assert_same(&exact_decimal(low));
        }
    }
}
