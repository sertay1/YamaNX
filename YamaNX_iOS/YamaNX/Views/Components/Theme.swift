import SwiftUI

public struct Theme {
    public static let bg = Color(red: 10/255, green: 12/255, blue: 18/255)
    public static let card = Color(red: 18/255, green: 20/255, blue: 32/255)
    public static let cardHover = Color(red: 26/255, green: 30/255, blue: 48/255)
    public static let primaryRed = Color(red: 229/255, green: 9/255, blue: 20/255)
    public static let neonPurple = Color(red: 168/255, green: 85/255, blue: 247/255)
    public static let neonCyan = Color(red: 0/255, green: 210/255, blue: 255/255)
    public static let stroke = Color(red: 35/255, green: 40/255, blue: 60/255)
    public static let strokeActive = Color(red: 229/255, green: 9/255, blue: 20/255).opacity(0.6)
    public static let textMuted = Color(red: 160/255, green: 170/255, blue: 195/255)
    public static let green = Color(red: 46/255, green: 204/255, blue: 113/255)
    public static let yellow = Color(red: 255/255, green: 215/255, blue: 0/255)
}

public extension View {
    func yxCardStyle(radius: CGFloat = 18, border: Color = Theme.stroke, borderWidth: CGFloat = 1) -> some View {
        self
            .background(Theme.card)
            .clipShape(RoundedRectangle(cornerRadius: radius))
            .overlay(
                RoundedRectangle(cornerRadius: radius)
                    .stroke(border, lineWidth: borderWidth)
            )
    }
}
